#include <string.h>

#include "tests.h"
#include "gremlind/lib.h"

static struct gremlind_arena make_arena(void)
{
	struct gremlind_arena a;
	bool ok = gremlind_arena_init_malloc(&a, 8192);
	ASSERT_TRUE(ok);
	return a;
}

static struct gremlind_build_result build(struct gremlind_arena *a, char *src)
{
	struct gremlinp_parser_buffer buf;
	gremlinp_parser_buffer_init(&buf, src, 0);
	return gremlind_build_file(a, &buf);
}

static TEST(name_parse_simple)
{
	struct gremlind_arena arena = make_arena();
	struct gremlind_scoped_name n;

	const char *s = "foo.bar.Baz";
	ASSERT_EQ(GREMLINP_OK, gremlind_scoped_name_parse(&arena, s, strlen(s), &n));
	ASSERT_EQ(3, n.n_segments);
	ASSERT_FALSE(n.absolute);
	ASSERT_SPAN_EQ("foo", n.segments[0].start, n.segments[0].length);
	ASSERT_SPAN_EQ("bar", n.segments[1].start, n.segments[1].length);
	ASSERT_SPAN_EQ("Baz", n.segments[2].start, n.segments[2].length);

	gremlind_arena_free_malloc(&arena);
}

static TEST(name_parse_absolute)
{
	struct gremlind_arena arena = make_arena();
	struct gremlind_scoped_name n;

	const char *s = ".foo.Bar";
	ASSERT_EQ(GREMLINP_OK, gremlind_scoped_name_parse(&arena, s, strlen(s), &n));
	ASSERT_TRUE(n.absolute);
	ASSERT_EQ(2, n.n_segments);
	ASSERT_SPAN_EQ("foo", n.segments[0].start, n.segments[0].length);
	ASSERT_SPAN_EQ("Bar", n.segments[1].start, n.segments[1].length);

	gremlind_arena_free_malloc(&arena);
}

static TEST(name_parse_rejects_empty_segments)
{
	struct gremlind_arena arena = make_arena();
	struct gremlind_scoped_name n;

	const char *a = "foo..bar";
	const char *b = "foo.";
	const char *c = "";
	const char *d = ".";
	ASSERT_TRUE(GREMLINP_OK != gremlind_scoped_name_parse(&arena, a, strlen(a), &n));
	ASSERT_TRUE(GREMLINP_OK != gremlind_scoped_name_parse(&arena, b, strlen(b), &n));
	ASSERT_TRUE(GREMLINP_OK != gremlind_scoped_name_parse(&arena, c, strlen(c), &n));
	ASSERT_TRUE(GREMLINP_OK != gremlind_scoped_name_parse(&arena, d, strlen(d), &n));

	gremlind_arena_free_malloc(&arena);
}

static TEST(name_eq_compares_segments)
{
	struct gremlind_arena arena = make_arena();
	struct gremlind_scoped_name a, b, c;

	const char *s1 = "foo.Bar";
	const char *s2 = "foo.Bar";
	const char *s3 = "foo.Baz";
	gremlind_scoped_name_parse(&arena, s1, strlen(s1), &a);
	gremlind_scoped_name_parse(&arena, s2, strlen(s2), &b);
	gremlind_scoped_name_parse(&arena, s3, strlen(s3), &c);

	ASSERT_TRUE(gremlind_scoped_name_eq(&a, &b));
	ASSERT_FALSE(gremlind_scoped_name_eq(&a, &c));

	gremlind_arena_free_malloc(&arena);
}

static TEST(compute_names_no_package_top_level)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"message M {}\n"
		"enum E { E0 = 0; }\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);
	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_scoped_names(&arena, r.file));

	struct gremlind_message *m = &r.file->messages.items[0];
	ASSERT_EQ(1, m->scoped_name.n_segments);
	ASSERT_SPAN_EQ("M", m->scoped_name.segments[0].start,
		       m->scoped_name.segments[0].length);

	struct gremlind_enum *e = &r.file->enums.items[0];
	ASSERT_EQ(1, e->scoped_name.n_segments);
	ASSERT_SPAN_EQ("E", e->scoped_name.segments[0].start,
		       e->scoped_name.segments[0].length);

	gremlind_arena_free_malloc(&arena);
}

static TEST(compute_names_with_package)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"package foo.bar;\n"
		"message M {}\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);
	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_scoped_names(&arena, r.file));

	struct gremlind_message *m = &r.file->messages.items[0];
	ASSERT_EQ(3, m->scoped_name.n_segments);
	ASSERT_SPAN_EQ("foo", m->scoped_name.segments[0].start,
		       m->scoped_name.segments[0].length);
	ASSERT_SPAN_EQ("bar", m->scoped_name.segments[1].start,
		       m->scoped_name.segments[1].length);
	ASSERT_SPAN_EQ("M", m->scoped_name.segments[2].start,
		       m->scoped_name.segments[2].length);

	gremlind_arena_free_malloc(&arena);
}

static TEST(compute_names_deep_nesting)
{
	/* The worked example: package foo.bar; message A { message B { message C {} } } */
	char src[] =
		"syntax = \"proto3\";\n"
		"package foo.bar;\n"
		"message A {\n"
		"  message B {\n"
		"    message C {\n"
		"      int32 n = 1;\n"
		"    }\n"
		"  }\n"
		"}\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);
	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_scoped_names(&arena, r.file));

	struct gremlind_message *a = &r.file->messages.items[0];
	struct gremlind_message *b = &r.file->messages.items[1];
	struct gremlind_message *c = &r.file->messages.items[2];

	ASSERT_EQ(3, a->scoped_name.n_segments);   /* foo.bar.A */
	ASSERT_EQ(4, b->scoped_name.n_segments);   /* foo.bar.A.B */
	ASSERT_EQ(5, c->scoped_name.n_segments);   /* foo.bar.A.B.C */

	ASSERT_SPAN_EQ("foo", c->scoped_name.segments[0].start, c->scoped_name.segments[0].length);
	ASSERT_SPAN_EQ("bar", c->scoped_name.segments[1].start, c->scoped_name.segments[1].length);
	ASSERT_SPAN_EQ("A",   c->scoped_name.segments[2].start, c->scoped_name.segments[2].length);
	ASSERT_SPAN_EQ("B",   c->scoped_name.segments[3].start, c->scoped_name.segments[3].length);
	ASSERT_SPAN_EQ("C",   c->scoped_name.segments[4].start, c->scoped_name.segments[4].length);

	gremlind_arena_free_malloc(&arena);
}

static TEST(compute_names_nested_enum_has_message_parent_name)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"package p;\n"
		"message M {\n"
		"  enum E { E0 = 0; }\n"
		"}\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);
	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_scoped_names(&arena, r.file));

	struct gremlind_enum *e = &r.file->enums.items[0];
	ASSERT_EQ(3, e->scoped_name.n_segments);   /* p.M.E */
	ASSERT_SPAN_EQ("p", e->scoped_name.segments[0].start, e->scoped_name.segments[0].length);
	ASSERT_SPAN_EQ("M", e->scoped_name.segments[1].start, e->scoped_name.segments[1].length);
	ASSERT_SPAN_EQ("E", e->scoped_name.segments[2].start, e->scoped_name.segments[2].length);

	gremlind_arena_free_malloc(&arena);
}

void name_test(void);

void name_test(void)
{
	RUN_TEST(name_parse_simple);
	RUN_TEST(name_parse_absolute);
	RUN_TEST(name_parse_rejects_empty_segments);
	RUN_TEST(name_eq_compares_segments);
	RUN_TEST(compute_names_no_package_top_level);
	RUN_TEST(compute_names_with_package);
	RUN_TEST(compute_names_deep_nesting);
	RUN_TEST(compute_names_nested_enum_has_message_parent_name);
}

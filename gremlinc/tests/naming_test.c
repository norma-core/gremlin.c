#include "tests.h"
#include "gremlinc/lib.h"

static struct gremlind_arena
make_arena(void)
{
	struct gremlind_arena a;
	int ok = gremlind_arena_init_malloc(&a, 4096);
	assert(ok);
	return a;
}

static TEST(c_keyword_detection_covers_all_44)
{
	/* spot-check a few */
	assert(gremlinc_is_c_keyword("int"));
	assert(gremlinc_is_c_keyword("struct"));
	assert(gremlinc_is_c_keyword("return"));
	assert(gremlinc_is_c_keyword("_Bool"));
	assert(gremlinc_is_c_keyword("_Alignas"));
	assert(!gremlinc_is_c_keyword("integer"));	/* not a keyword */
	assert(!gremlinc_is_c_keyword("Int"));		/* case-sensitive */
	assert(!gremlinc_is_c_keyword("my_field"));
}

static TEST(mangle_keyword_gets_underscore_suffix)
{
	struct gremlind_arena arena = make_arena();
	struct gremlinc_name_scope s;
	gremlinc_name_scope_init(&s, &arena);

	const char *n = gremlinc_name_scope_mangle(&s, "int", 3);
	ASSERT_STR_EQ("int_", n);

	n = gremlinc_name_scope_mangle(&s, "struct", 6);
	ASSERT_STR_EQ("struct_", n);

	gremlind_arena_free_malloc(&arena);
}

static TEST(mangle_collision_appends_incrementing_suffix)
{
	struct gremlind_arena arena = make_arena();
	struct gremlinc_name_scope s;
	gremlinc_name_scope_init(&s, &arena);

	ASSERT_STR_EQ("Foo",   gremlinc_name_scope_mangle(&s, "Foo", 3));
	ASSERT_STR_EQ("Foo_1", gremlinc_name_scope_mangle(&s, "Foo", 3));
	ASSERT_STR_EQ("Foo_2", gremlinc_name_scope_mangle(&s, "Foo", 3));

	gremlind_arena_free_malloc(&arena);
}

static TEST(mangle_keyword_and_collision_compose)
{
	/* proto `int` + later proto `int` → int_, int__1 */
	struct gremlind_arena arena = make_arena();
	struct gremlinc_name_scope s;
	gremlinc_name_scope_init(&s, &arena);

	ASSERT_STR_EQ("int_",   gremlinc_name_scope_mangle(&s, "int", 3));
	ASSERT_STR_EQ("int__1", gremlinc_name_scope_mangle(&s, "int", 3));

	gremlind_arena_free_malloc(&arena);
}

static TEST(cname_for_type_uses_scope)
{
	/* Two proto types that flatten to the same C name — second gets _1. */
	char src[] =
		"syntax = \"proto3\";\n"
		"enum A_B_C { X = 0; }\n"		/* scoped = [A_B_C] → A_B_C */
		"message M {\n"
		"  enum A_B_C { Y = 0; }\n"		/* scoped = [M, A_B_C] → M_A_B_C (no collision) */
		"}\n";

	struct gremlind_arena arena = make_arena();
	struct gremlinp_parser_buffer buf;
	gremlinp_parser_buffer_init(&buf, src, 0);
	struct gremlind_build_result r = gremlind_build_file(&arena, &buf);
	assert(r.error == GREMLINP_OK);
	assert(gremlind_compute_scoped_names(&arena, r.file) == GREMLINP_OK);

	struct gremlinc_name_scope s;
	gremlinc_name_scope_init(&s, &arena);

	const char *a = gremlinc_cname_for_type(&s, &r.file->enums.items[0].scoped_name);
	const char *b = gremlinc_cname_for_type(&s, &r.file->enums.items[1].scoped_name);

	ASSERT_STR_EQ("A_B_C",   a);
	ASSERT_STR_EQ("M_A_B_C", b);

	/* Register a second time for the SAME scoped_name — collision path. */
	const char *c = gremlinc_cname_for_type(&s, &r.file->enums.items[0].scoped_name);
	ASSERT_STR_EQ("A_B_C_1", c);

	gremlind_arena_free_malloc(&arena);
}

void naming_test(void);

void naming_test(void)
{
	RUN_TEST(c_keyword_detection_covers_all_44);
	RUN_TEST(mangle_keyword_gets_underscore_suffix);
	RUN_TEST(mangle_collision_appends_incrementing_suffix);
	RUN_TEST(mangle_keyword_and_collision_compose);
	RUN_TEST(cname_for_type_uses_scope);
}

#include <string.h>

#include "tests.h"
#include "gremlind/lib.h"

static struct gremlind_arena make_arena(void)
{
	struct gremlind_arena a;
	bool ok = gremlind_arena_init_malloc(&a, 4096);
	ASSERT_TRUE(ok);
	return a;
}

static struct gremlind_build_result build(struct gremlind_arena *a, char *src)
{
	struct gremlinp_parser_buffer buf;
	gremlinp_parser_buffer_init(&buf, src, 0);
	return gremlind_build_file(a, &buf);
}

static TEST(build_empty_file_has_no_entries)
{
	char src[] = "";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);

	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_NOT_NULL(r.file);
	ASSERT_FALSE(r.file->syntax.present);
	ASSERT_FALSE(r.file->edition.present);
	ASSERT_FALSE(r.file->package.present);
	ASSERT_EQ(0, r.file->imports.count);
	ASSERT_NULL(r.file->imports.items);

	gremlind_arena_free_malloc(&arena);
}

static TEST(build_proto3_syntax_only)
{
	char src[] = "syntax = \"proto3\";\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);

	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_NOT_NULL(r.file);
	ASSERT_TRUE(r.file->syntax.present);
	ASSERT_FALSE(r.file->edition.present);
	ASSERT_EQ(6, r.file->syntax.value.version_length);
	ASSERT_TRUE(memcmp(r.file->syntax.value.version_start, "proto3", 6) == 0);

	gremlind_arena_free_malloc(&arena);
}

static TEST(build_edition_2023_only)
{
	char src[] = "edition = \"2023\";\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);

	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_NOT_NULL(r.file);
	ASSERT_TRUE(r.file->edition.present);
	ASSERT_FALSE(r.file->syntax.present);
	ASSERT_EQ(4, r.file->edition.value.edition_length);
	ASSERT_TRUE(memcmp(r.file->edition.value.edition_start, "2023", 4) == 0);

	gremlind_arena_free_malloc(&arena);
}

static TEST(build_syntax_package)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"package example.api;\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);

	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_TRUE(r.file->syntax.present);
	ASSERT_TRUE(r.file->package.present);
	ASSERT_EQ(11, r.file->package.value.name_length);
	ASSERT_TRUE(memcmp(r.file->package.value.name_start, "example.api", 11) == 0);

	gremlind_arena_free_malloc(&arena);
}

static TEST(build_imports_preserved_in_order_and_unresolved)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"import \"google/protobuf/timestamp.proto\";\n"
		"import public \"other.proto\";\n"
		"import weak \"legacy.proto\";\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);

	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(3, r.file->imports.count);

	ASSERT_EQ(GREMLINP_IMPORT_TYPE_REGULAR, r.file->imports.items[0].parsed.type);
	ASSERT_EQ(strlen("google/protobuf/timestamp.proto"),
		  r.file->imports.items[0].parsed.path_length);
	ASSERT_TRUE(memcmp(r.file->imports.items[0].parsed.path_start,
		   "google/protobuf/timestamp.proto",
		   strlen("google/protobuf/timestamp.proto")) == 0);
	ASSERT_NULL(r.file->imports.items[0].resolved);

	ASSERT_EQ(GREMLINP_IMPORT_TYPE_PUBLIC, r.file->imports.items[1].parsed.type);
	ASSERT_EQ(GREMLINP_IMPORT_TYPE_WEAK,   r.file->imports.items[2].parsed.type);

	gremlind_arena_free_malloc(&arena);
}

static TEST(build_single_enum_with_values)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"enum Status {\n"
		"  UNKNOWN = 0;\n"
		"  ACTIVE = 1;\n"
		"  INACTIVE = 2;\n"
		"}\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);

	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(1, r.file->enums.count);
	ASSERT_SPAN_EQ("Status", r.file->enums.items[0].parsed.name_start,
		       r.file->enums.items[0].parsed.name_length);
	ASSERT_EQ(3, r.file->enums.items[0].values.count);
	ASSERT_EQ(0, r.file->enums.items[0].values.items[0].parsed.index);
	ASSERT_EQ(1, r.file->enums.items[0].values.items[1].parsed.index);
	ASSERT_EQ(2, r.file->enums.items[0].values.items[2].parsed.index);
	ASSERT_SPAN_EQ("UNKNOWN",
		r.file->enums.items[0].values.items[0].parsed.name_start,
		r.file->enums.items[0].values.items[0].parsed.name_length);

	gremlind_arena_free_malloc(&arena);
}

static TEST(build_multiple_enums_preserved_in_order)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"enum A { A_ZERO = 0; }\n"
		"enum B { B_ZERO = 0; B_ONE = 1; }\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);

	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(2, r.file->enums.count);
	ASSERT_SPAN_EQ("A", r.file->enums.items[0].parsed.name_start,
		       r.file->enums.items[0].parsed.name_length);
	ASSERT_SPAN_EQ("B", r.file->enums.items[1].parsed.name_start,
		       r.file->enums.items[1].parsed.name_length);
	ASSERT_EQ(1, r.file->enums.items[0].values.count);
	ASSERT_EQ(2, r.file->enums.items[1].values.count);

	gremlind_arena_free_malloc(&arena);
}

static TEST(build_enum_ignores_body_options_and_reserved)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"enum E {\n"
		"  option allow_alias = true;\n"
		"  reserved 2, 15;\n"
		"  reserved \"OLD\";\n"
		"  FIRST = 0;\n"
		"  SECOND = 1;\n"
		"}\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);

	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(1, r.file->enums.count);
	ASSERT_EQ(2, r.file->enums.items[0].values.count);
	ASSERT_SPAN_EQ("FIRST",
		r.file->enums.items[0].values.items[0].parsed.name_start,
		r.file->enums.items[0].values.items[0].parsed.name_length);

	gremlind_arena_free_malloc(&arena);
}

static TEST(build_message_with_fields)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"message User {\n"
		"  string name = 1;\n"
		"  int32 id = 2;\n"
		"}\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);

	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(1, r.file->messages.count);
	ASSERT_SPAN_EQ("User", r.file->messages.items[0].parsed.name_start,
		       r.file->messages.items[0].parsed.name_length);
	ASSERT_EQ(2, r.file->messages.items[0].fields.count);
	ASSERT_EQ(1, r.file->messages.items[0].fields.items[0].parsed.index);
	ASSERT_EQ(2, r.file->messages.items[0].fields.items[1].parsed.index);
	ASSERT_NULL(r.file->messages.items[0].parent);

	gremlind_arena_free_malloc(&arena);
}

static TEST(build_nested_enum_flat_with_parent)
{
	/* Flat storage: the nested enum lives on file.enums, not inside the
	 * message; its parent points back to the message. */
	char src[] =
		"syntax = \"proto3\";\n"
		"message M {\n"
		"  enum Color { RED = 0; BLUE = 1; }\n"
		"  Color c = 1;\n"
		"}\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);

	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(1, r.file->messages.count);
	ASSERT_EQ(1, r.file->enums.count);
	ASSERT_SPAN_EQ("M", r.file->messages.items[0].parsed.name_start,
		       r.file->messages.items[0].parsed.name_length);
	ASSERT_SPAN_EQ("Color", r.file->enums.items[0].parsed.name_start,
		       r.file->enums.items[0].parsed.name_length);
	ASSERT_TRUE(r.file->enums.items[0].parent == &r.file->messages.items[0]);
	ASSERT_EQ(2, r.file->enums.items[0].values.count);
	ASSERT_EQ(1, r.file->messages.items[0].fields.count);

	gremlind_arena_free_malloc(&arena);
}

static TEST(build_nested_message_flat_in_declaration_order)
{
	/* Outer comes first, Inner right after — DFS pre-order. */
	char src[] =
		"syntax = \"proto3\";\n"
		"message Outer {\n"
		"  message Inner {\n"
		"    string x = 1;\n"
		"  }\n"
		"  Inner inner = 1;\n"
		"}\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);

	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(2, r.file->messages.count);
	ASSERT_SPAN_EQ("Outer", r.file->messages.items[0].parsed.name_start,
		       r.file->messages.items[0].parsed.name_length);
	ASSERT_SPAN_EQ("Inner", r.file->messages.items[1].parsed.name_start,
		       r.file->messages.items[1].parsed.name_length);
	ASSERT_NULL(r.file->messages.items[0].parent);
	ASSERT_TRUE(r.file->messages.items[1].parent == &r.file->messages.items[0]);
	ASSERT_EQ(1, r.file->messages.items[0].fields.count);
	ASSERT_EQ(1, r.file->messages.items[1].fields.count);

	gremlind_arena_free_malloc(&arena);
}

static TEST(build_deeply_nested_messages_flat_dfs_order)
{
	char src[] =
		"syntax = \"proto3\";\n"
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

	ASSERT_EQ(3, r.file->messages.count);
	struct gremlind_message *a = &r.file->messages.items[0];
	struct gremlind_message *b = &r.file->messages.items[1];
	struct gremlind_message *c = &r.file->messages.items[2];
	ASSERT_SPAN_EQ("A", a->parsed.name_start, a->parsed.name_length);
	ASSERT_SPAN_EQ("B", b->parsed.name_start, b->parsed.name_length);
	ASSERT_SPAN_EQ("C", c->parsed.name_start, c->parsed.name_length);
	ASSERT_NULL(a->parent);
	ASSERT_TRUE(b->parent == a);
	ASSERT_TRUE(c->parent == b);
	ASSERT_EQ(1, c->fields.count);

	gremlind_arena_free_malloc(&arena);
}

static TEST(build_mixed_top_and_nested_enums_all_on_file)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"enum TopA { TA = 0; }\n"
		"message M {\n"
		"  enum NestedA { NA = 0; }\n"
		"  message Inner {\n"
		"    enum NestedB { NB = 0; }\n"
		"  }\n"
		"}\n"
		"enum TopB { TB = 0; }\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);
	ASSERT_EQ(GREMLINP_OK, r.error);

	/* All four enums flat on the file, DFS order:
	 * TopA (top), NestedA (under M), NestedB (under Inner), TopB (top). */
	ASSERT_EQ(4, r.file->enums.count);
	ASSERT_SPAN_EQ("TopA", r.file->enums.items[0].parsed.name_start,
		       r.file->enums.items[0].parsed.name_length);
	ASSERT_NULL(r.file->enums.items[0].parent);
	ASSERT_SPAN_EQ("NestedA", r.file->enums.items[1].parsed.name_start,
		       r.file->enums.items[1].parsed.name_length);
	ASSERT_SPAN_EQ("NestedB", r.file->enums.items[2].parsed.name_start,
		       r.file->enums.items[2].parsed.name_length);
	ASSERT_SPAN_EQ("TopB", r.file->enums.items[3].parsed.name_start,
		       r.file->enums.items[3].parsed.name_length);
	ASSERT_NULL(r.file->enums.items[3].parent);
	ASSERT_EQ(2, r.file->messages.count);

	gremlind_arena_free_malloc(&arena);
}

static TEST(build_propagates_parse_error)
{
	char src[] = "syntax = \"proto9\";\n";
	struct gremlind_arena arena = make_arena();

	struct gremlind_build_result r = build(&arena, src);

	ASSERT_TRUE(r.error != GREMLINP_OK);
	ASSERT_NULL(r.file);

	gremlind_arena_free_malloc(&arena);
}

void build_test(void);

void build_test(void)
{
	RUN_TEST(build_empty_file_has_no_entries);
	RUN_TEST(build_proto3_syntax_only);
	RUN_TEST(build_edition_2023_only);
	RUN_TEST(build_syntax_package);
	RUN_TEST(build_imports_preserved_in_order_and_unresolved);
	RUN_TEST(build_single_enum_with_values);
	RUN_TEST(build_multiple_enums_preserved_in_order);
	RUN_TEST(build_enum_ignores_body_options_and_reserved);
	RUN_TEST(build_message_with_fields);
	RUN_TEST(build_nested_enum_flat_with_parent);
	RUN_TEST(build_nested_message_flat_in_declaration_order);
	RUN_TEST(build_deeply_nested_messages_flat_dfs_order);
	RUN_TEST(build_mixed_top_and_nested_enums_all_on_file);
	RUN_TEST(build_propagates_parse_error);
}

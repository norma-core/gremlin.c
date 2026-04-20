#include "tests.h"
#include "gremlinc/lib.h"

static struct gremlind_file *
build_from_src(struct gremlind_arena *arena, char *src)
{
	struct gremlinp_parser_buffer buf;
	gremlinp_parser_buffer_init(&buf, src, 0);
	struct gremlind_build_result r = gremlind_build_file(arena, &buf);
	if (r.error != GREMLINP_OK) return NULL;
	if (gremlind_compute_scoped_names(arena, r.file) != GREMLINP_OK) return NULL;
	return r.file;
}

static TEST(emit_top_level_enum_no_package)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"enum Color { UNKNOWN = 0; RED = 1; BLUE = 2; }\n";

	struct gremlind_arena arena;
	ASSERT_EQ(1, gremlind_arena_init_malloc(&arena, 8192));
	struct gremlind_file *file = build_from_src(&arena, src);
	assert(file != NULL);

	struct gremlinc_name_scope s;
	gremlinc_name_scope_init(&s, &arena);
	ASSERT_EQ(GREMLINP_OK, gremlinc_assign_c_names(&s, file));

	char out[512];
	struct gremlinc_writer w;
	gremlinc_writer_init(&w, out, sizeof out - 1);
	ASSERT_EQ(GREMLINP_OK, gremlinc_emit_enum(&w, &s, &file->enums.items[0]));
	out[w.offset] = '\0';

	const char *expected =
		"typedef enum Color {\n"
		"\tColor_UNKNOWN = 0,\n"
		"\tColor_RED = 1,\n"
		"\tColor_BLUE = 2,\n"
		"} Color;\n\n";
	ASSERT_STR_EQ(expected, out);

	gremlind_arena_free_malloc(&arena);
}

static TEST(emit_enum_with_c_keyword_value_gets_mangled)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"enum K { K_int = 0; K_struct = 1; K_ok = 2; }\n";

	struct gremlind_arena arena;
	ASSERT_EQ(1, gremlind_arena_init_malloc(&arena, 8192));
	struct gremlind_file *file = build_from_src(&arena, src);
	assert(file != NULL);

	struct gremlinc_name_scope s;
	gremlinc_name_scope_init(&s, &arena);
	ASSERT_EQ(GREMLINP_OK, gremlinc_assign_c_names(&s, file));

	char out[512];
	struct gremlinc_writer w;
	gremlinc_writer_init(&w, out, sizeof out - 1);
	ASSERT_EQ(GREMLINP_OK, gremlinc_emit_enum(&w, &s, &file->enums.items[0]));
	out[w.offset] = '\0';

	/* Proto value names are `K_int`, `K_struct`, `K_ok` — already
	 * prefixed so no keyword collision. Test is that composition of
	 * typedef + value works cleanly; keyword mangle is covered
	 * separately in naming_test.c. */
	const char *expected =
		"typedef enum K {\n"
		"\tK_K_int = 0,\n"
		"\tK_K_struct = 1,\n"
		"\tK_K_ok = 2,\n"
		"} K;\n\n";
	ASSERT_STR_EQ(expected, out);

	gremlind_arena_free_malloc(&arena);
}

static TEST(emit_two_enums_with_flat_name_collision)
{
	/* Two proto types flattening to the same C identifier — scope
	 * disambiguates the second by appending `_1`. */
	char src[] =
		"syntax = \"proto3\";\n"
		"enum A_B { X = 0; }\n"
		"message A { enum B { Y = 0; } }\n";

	struct gremlind_arena arena;
	ASSERT_EQ(1, gremlind_arena_init_malloc(&arena, 8192));
	struct gremlind_file *file = build_from_src(&arena, src);
	assert(file != NULL);

	struct gremlinc_name_scope s;
	gremlinc_name_scope_init(&s, &arena);
	ASSERT_EQ(GREMLINP_OK, gremlinc_assign_c_names(&s, file));

	char out[512];
	struct gremlinc_writer w;
	gremlinc_writer_init(&w, out, sizeof out - 1);

	ASSERT_EQ(GREMLINP_OK, gremlinc_emit_enum(&w, &s, &file->enums.items[0]));
	ASSERT_EQ(GREMLINP_OK, gremlinc_emit_enum(&w, &s, &file->enums.items[1]));
	out[w.offset] = '\0';

	/* First enum gets A_B; second (proto A.B) would also flatten to
	 * A_B so it gets A_B_1. Enum values for the second also roll up. */
	const char *expected =
		"typedef enum A_B {\n"
		"\tA_B_X = 0,\n"
		"} A_B;\n"
		"\n"
		"typedef enum A_B_1 {\n"
		"\tA_B_1_Y = 0,\n"
		"} A_B_1;\n"
		"\n";
	ASSERT_STR_EQ(expected, out);

	gremlind_arena_free_malloc(&arena);
}

void enum_test(void);

void enum_test(void)
{
	RUN_TEST(emit_top_level_enum_no_package);
	RUN_TEST(emit_enum_with_c_keyword_value_gets_mangled);
	RUN_TEST(emit_two_enums_with_flat_name_collision);
}

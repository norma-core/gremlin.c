#include <string.h>

#include "tests.h"
#include "gremlinc/lib.h"
#include "gremlind/lib.h"

static struct gremlind_file *
build(struct gremlind_arena *arena, struct gremlind_source *gs, char *src)
{
	struct gremlind_resolve_context ctx;

	gs->path = "t.proto";
	gs->path_len = 7;
	gremlinp_parser_buffer_init(&gs->buf, src, 0);
	gremlind_resolve_context_init(&ctx, arena, gs, 1);
	if (gremlind_build_all(&ctx) != GREMLINP_OK ||
	    gremlind_link_imports(&ctx) != GREMLINP_OK ||
	    gremlind_compute_scoped_names(arena, ctx.files[0]) != GREMLINP_OK ||
	    gremlind_compute_visibility(&ctx) != GREMLINP_OK ||
	    gremlind_resolve_type_refs(&ctx) != GREMLINP_OK)
		return NULL;
	return ctx.files[0];
}

/* An arena handed over full of garbage, as a caller's own buffer may be:
 * a name nothing has set yet must read as unset. */
static TEST(emit_message_from_a_garbage_arena)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"enum E { E_NONE = 0; }\n"
		"message A { B b = 1; E e = 2; }\n"
		"message B { A a = 1; bytes raw = 2; }\n";
	static char chunk[64 * 1024];
	memset(chunk, 0xa5, sizeof chunk);

	struct gremlind_arena arena;
	gremlind_arena_init(&arena, chunk, sizeof chunk, NULL, NULL);
	struct gremlind_source gs;
	struct gremlind_file *file = build(&arena, &gs, src);
	ASSERT_NOT_NULL(file);
	ASSERT_NULL(file->enums.items[0].c_name);

	struct gremlinc_name_scope s;
	gremlinc_name_scope_init(&s, &arena);
	ASSERT_EQ(GREMLINP_OK, gremlinc_assign_c_names(&s, file));
	ASSERT_NULL(file->messages.items[0].c_reader_ok);

	struct gremlinc_writer w;
	ASSERT_TRUE(gremlinc_writer_init_owned(&w, 4096));
	ASSERT_EQ(GREMLINP_OK, gremlinc_emit_message(&w, &s, NULL, &file->messages.items[0], NULL));
	ASSERT_EQ(GREMLINP_OK, gremlinc_write(&w, "", 1));
	/* B is not emitted yet, so A's getter names B's predicate by default. */
	ASSERT_NOT_NULL(strstr(w.buf, "B_reader_ok(out, out->src_len)"));
	gremlinc_writer_dispose(&w);
}

/* A predicate name never takes a public name a type or an enum value would
 * get: here the enum value comes first and the predicate is renamed. */
static TEST(predicate_does_not_take_an_enum_value_name)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"enum Foo { bar_reader_ok = 0; }\n"
		"message Foo_bar { int32 x = 1; }\n";
	struct gremlind_arena arena;
	ASSERT_EQ(1, gremlind_arena_init_malloc(&arena, 64 * 1024));
	struct gremlind_source gs;
	struct gremlind_file *file = build(&arena, &gs, src);
	ASSERT_NOT_NULL(file);

	struct gremlinc_name_scope s;
	gremlinc_name_scope_init(&s, &arena);
	ASSERT_EQ(GREMLINP_OK, gremlinc_assign_c_names(&s, file));
	struct gremlinc_writer w;
	ASSERT_TRUE(gremlinc_writer_init_owned(&w, 4096));
	ASSERT_EQ(GREMLINP_OK, gremlinc_emit_enum(&w, &s, &file->enums.items[0]));
	ASSERT_EQ(GREMLINP_OK, gremlinc_emit_message(&w, &s, NULL, &file->messages.items[0], NULL));
	ASSERT_EQ(GREMLINP_OK, gremlinc_write(&w, "", 1));
	ASSERT_NOT_NULL(strstr(w.buf, "\tFoo_bar_reader_ok = 0,"));
	ASSERT_STR_EQ("Foo_bar_reader_ok_1", file->messages.items[0].c_reader_ok);
	gremlinc_writer_dispose(&w);
	gremlind_arena_free_malloc(&arena);
}

/* A field named <f>_iter_ok keeps the getter it had before contracts. */
static TEST(iter_predicate_does_not_take_a_getter_name)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"message A { repeated bytes get_x = 1; bytes x_iter_ok = 2; }\n";
	struct gremlind_arena arena;
	ASSERT_EQ(1, gremlind_arena_init_malloc(&arena, 64 * 1024));
	struct gremlind_source gs;
	struct gremlind_file *file = build(&arena, &gs, src);
	ASSERT_NOT_NULL(file);

	struct gremlinc_name_scope s;
	gremlinc_name_scope_init(&s, &arena);
	ASSERT_EQ(GREMLINP_OK, gremlinc_assign_c_names(&s, file));
	struct gremlinc_writer w;
	ASSERT_TRUE(gremlinc_writer_init_owned(&w, 4096));
	ASSERT_EQ(GREMLINP_OK, gremlinc_emit_message(&w, &s, NULL, &file->messages.items[0], NULL));
	ASSERT_EQ(GREMLINP_OK, gremlinc_write(&w, "", 1));
	ASSERT_NOT_NULL(strstr(w.buf, "\nA_reader_get_x_iter_ok(const A_reader *r)"));
	ASSERT_NOT_NULL(strstr(w.buf, "predicate A_reader_get_x_iter_ok_1("));
	gremlinc_writer_dispose(&w);
	gremlind_arena_free_malloc(&arena);
}

/* Imported enum values keep the names they had before contracts, whichever
 * file is emitted first: plain, and pushed to _1 by a type of the same name. */
static TEST(predicate_does_not_take_an_imported_enum_value_name)
{
	char a[] = "syntax = \"proto3\";\nenum Foo { bar_reader_ok = 0; }\n"
		   "enum Baz { qux_reader_ok = 0; }\nmessage Baz_qux_reader_ok { int32 n = 1; }\n";
	char b[] = "syntax = \"proto3\";\nimport \"a.proto\";\n"
		   "message Foo_bar { int32 x = 1; }\nmessage Baz_qux { int32 y = 1; }\n";
	struct gremlind_arena arena;
	ASSERT_EQ(1, gremlind_arena_init_malloc(&arena, 256 * 1024));
	struct gremlind_source gs[2] = { { "a.proto", 7, { 0 } }, { "b.proto", 7, { 0 } } };
	gremlinp_parser_buffer_init(&gs[0].buf, a, 0);
	gremlinp_parser_buffer_init(&gs[1].buf, b, 0);
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, gs, 2);
	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	for (size_t i = 0; i < 2; i++)
		ASSERT_EQ(GREMLINP_OK, gremlind_compute_scoped_names(&arena, ctx.files[i]));
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_visibility(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_resolve_type_refs(&ctx));

	struct gremlinc_name_scope s;
	gremlinc_name_scope_init(&s, &arena);
	for (size_t i = 0; i < 2; i++)
		ASSERT_EQ(GREMLINP_OK, gremlinc_assign_c_names(&s, ctx.files[i]));
	struct gremlinc_name_scope reserved = s;
	for (size_t i = 0; i < 2; i++)
		ASSERT_EQ(GREMLINP_OK, gremlinc_reserve_public_names(&reserved, ctx.files[i]));
	struct gremlinc_writer w;
	ASSERT_TRUE(gremlinc_writer_init_owned(&w, 8192));
	for (size_t i = 0; i < 2; i++)
		ASSERT_EQ(GREMLINP_OK, gremlinc_emit_message(&w, &s, &reserved, &ctx.files[1]->messages.items[i], NULL));
	for (size_t i = 0; i < 2; i++)
		ASSERT_EQ(GREMLINP_OK, gremlinc_emit_enum(&w, &s, &ctx.files[0]->enums.items[i]));
	ASSERT_EQ(GREMLINP_OK, gremlinc_write(&w, "", 1));
	ASSERT_NOT_NULL(strstr(w.buf, "\tFoo_bar_reader_ok = 0,"));
	ASSERT_NOT_NULL(strstr(w.buf, "\tBaz_qux_reader_ok_1 = 0,"));
	ASSERT_STR_EQ("Foo_bar_reader_ok_1", ctx.files[1]->messages.items[0].c_reader_ok);
	ASSERT_STR_EQ("Baz_qux_reader_ok_2", ctx.files[1]->messages.items[1].c_reader_ok);
	gremlinc_writer_dispose(&w);
	gremlind_arena_free_malloc(&arena);
}

/* a.proto and b.proto, b importing a, built and named as gremlinc-gen does. */
static struct gremlind_resolve_context *
build2(struct gremlind_arena *arena, struct gremlind_source *gs, char *a, char *b,
       struct gremlinc_name_scope *s, struct gremlinc_name_scope *logic,
       struct gremlinc_name_scope *reserved)
{
	static struct gremlind_resolve_context ctx;

	gs[0].path = "a.proto";
	gs[0].path_len = 7;
	gs[1].path = "b.proto";
	gs[1].path_len = 7;
	gremlinp_parser_buffer_init(&gs[0].buf, a, 0);
	gremlinp_parser_buffer_init(&gs[1].buf, b, 0);
	gremlind_resolve_context_init(&ctx, arena, gs, 2);
	if (gremlind_build_all(&ctx) != GREMLINP_OK ||
	    gremlind_link_imports(&ctx) != GREMLINP_OK ||
	    gremlind_compute_scoped_names(arena, ctx.files[0]) != GREMLINP_OK ||
	    gremlind_compute_scoped_names(arena, ctx.files[1]) != GREMLINP_OK ||
	    gremlind_compute_visibility(&ctx) != GREMLINP_OK ||
	    gremlind_resolve_type_refs(&ctx) != GREMLINP_OK)
		return NULL;
	gremlinc_name_scope_init(s, arena);
	gremlinc_name_scope_init(logic, arena);
	gremlinc_name_scope_init(reserved, arena);
	for (size_t i = 0; i < 2; i++) {
		if (gremlinc_assign_c_names(s, ctx.files[i]) != GREMLINP_OK ||
		    gremlinc_reserve_public_names(reserved, ctx.files[i]) != GREMLINP_OK)
			return NULL;
	}
	return &ctx;
}

/* Emits the first message of each file, each with its file's write plan. */
static bool
emit2(struct gremlinc_writer *w, struct gremlind_arena *arena,
      struct gremlind_resolve_context *ctx, struct gremlinc_name_scope *s,
      struct gremlinc_name_scope *logic, const struct gremlinc_name_scope *reserved)
{
	for (size_t i = 0; i < 2; i++) {
		struct gremlind_message *m = &ctx->files[i]->messages.items[0];
		struct gremlinc_encode_plan *plan;
		if (gremlinc_encode_plan_build(arena, logic, reserved, &m, 1, &plan) != GREMLINP_OK ||
		    gremlinc_emit_message(w, s, reserved, m, plan) != GREMLINP_OK)
			return false;
	}
	return gremlinc_write(w, "", 1) == GREMLINP_OK;
}

/* A write-side ghost is a C function, so it must leave a getter its name,
 * even one of a file emitted later. */
static TEST(ghost_does_not_take_a_later_getter_name)
{
	char a[] = "syntax = \"proto3\";\nmessage A { repeated uint64 b_reader_get_x = 1; }\n";
	char b[] = "syntax = \"proto3\";\nimport \"a.proto\";\nmessage A_b { uint64 x_mono = 1; }\n";
	struct gremlind_arena arena;
	ASSERT_EQ(1, gremlind_arena_init_malloc(&arena, 256 * 1024));
	struct gremlind_source gs[2];
	struct gremlinc_name_scope s, logic, reserved;
	struct gremlind_resolve_context *ctx = build2(&arena, gs, a, b, &s, &logic, &reserved);
	ASSERT_NOT_NULL(ctx);
	struct gremlinc_writer w;
	ASSERT_TRUE(gremlinc_writer_init_owned(&w, 4096));
	ASSERT_TRUE(emit2(&w, &arena, ctx, &s, &logic, &reserved));
	ASSERT_NOT_NULL(strstr(w.buf, "\nA_b_reader_get_x_mono(const A_b_reader *r)"));
	gremlinc_writer_dispose(&w);
	gremlind_arena_free_malloc(&arena);
}

/* b.pb.h includes a.pb.h, so a lemma name may appear in only one of them. */
static TEST(lemma_names_are_unique_across_files)
{
	char a[] = "syntax = \"proto3\";\nmessage A { uint32 b_x = 1; }\n";
	char b[] = "syntax = \"proto3\";\nimport \"a.proto\";\nmessage A_b { uint32 x = 1; }\n";
	struct gremlind_arena arena;
	ASSERT_EQ(1, gremlind_arena_init_malloc(&arena, 256 * 1024));
	struct gremlind_source gs[2];
	struct gremlinc_name_scope s, logic, reserved;
	struct gremlind_resolve_context *ctx = build2(&arena, gs, a, b, &s, &logic, &reserved);
	ASSERT_NOT_NULL(ctx);
	struct gremlinc_writer w;
	ASSERT_TRUE(gremlinc_writer_init_owned(&w, 4096));
	ASSERT_TRUE(emit2(&w, &arena, ctx, &s, &logic, &reserved));
	ASSERT_NOT_NULL(strstr(w.buf, "lemma A_b_x_size_frame{"));
	ASSERT_NOT_NULL(strstr(w.buf, "lemma A_b_x_size_frame_1{"));
	gremlinc_writer_dispose(&w);
	gremlind_arena_free_malloc(&arena);
}

/* Frama-C reads a typedef named like a variable of the annotations as a type
 * in every later annotation, so such a type takes a suffix. */
static TEST(type_named_like_an_annotation_variable_gets_a_suffix)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"message lo { uint32 f = 1; }\n"
		"message Holder { lo a = 1; uint64 z = 2; }\n";
	struct gremlind_arena arena;
	ASSERT_EQ(1, gremlind_arena_init_malloc(&arena, 256 * 1024));
	struct gremlind_source gs;
	struct gremlind_file *file = build(&arena, &gs, src);
	ASSERT_NOT_NULL(file);

	struct gremlinc_name_scope s, logic;
	gremlinc_name_scope_init(&s, &arena);
	gremlinc_name_scope_init(&logic, &arena);
	ASSERT_EQ(GREMLINP_OK, gremlinc_name_scope_reserve_logic(&s));
	ASSERT_EQ(GREMLINP_OK, gremlinc_assign_c_names(&s, file));
	ASSERT_STR_EQ("lo_1", file->messages.items[0].c_name);
	struct gremlind_message *order[2] = { &file->messages.items[0], &file->messages.items[1] };
	struct gremlinc_encode_plan *plan;
	ASSERT_EQ(GREMLINP_OK, gremlinc_encode_plan_build(&arena, &logic, NULL, order, 2, &plan));
	struct gremlinc_writer w;
	ASSERT_TRUE(gremlinc_writer_init_owned(&w, 4096));
	for (size_t i = 0; i < 2; i++)
		ASSERT_EQ(GREMLINP_OK, gremlinc_emit_message(&w, &s, NULL, order[i], plan));
	ASSERT_EQ(GREMLINP_OK, gremlinc_write(&w, "", 1));
	ASSERT_NOT_NULL(strstr(w.buf, "void lo_1_tsize_nonneg(lo_1 *m)"));
	ASSERT_NOT_NULL(strstr(w.buf, "void Holder_tsize_nonneg(Holder *m)"));
	gremlinc_writer_dispose(&w);
	gremlind_arena_free_malloc(&arena);
}

void message_test(void);

void message_test(void)
{
	RUN_TEST(emit_message_from_a_garbage_arena);
	RUN_TEST(predicate_does_not_take_an_enum_value_name);
	RUN_TEST(iter_predicate_does_not_take_a_getter_name);
	RUN_TEST(predicate_does_not_take_an_imported_enum_value_name);
	RUN_TEST(ghost_does_not_take_a_later_getter_name);
	RUN_TEST(lemma_names_are_unique_across_files);
	RUN_TEST(type_named_like_an_annotation_variable_gets_a_suffix);
}

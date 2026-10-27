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
	ASSERT_EQ(GREMLINP_OK, gremlinc_emit_message(&w, &s, &file->messages.items[0]));
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
	ASSERT_EQ(GREMLINP_OK, gremlinc_emit_message(&w, &s, &file->messages.items[0]));
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
	ASSERT_EQ(GREMLINP_OK, gremlinc_emit_message(&w, &s, &file->messages.items[0]));
	ASSERT_EQ(GREMLINP_OK, gremlinc_write(&w, "", 1));
	ASSERT_NOT_NULL(strstr(w.buf, "\nA_reader_get_x_iter_ok(const A_reader *r)"));
	ASSERT_NOT_NULL(strstr(w.buf, "predicate A_reader_get_x_iter_ok_1("));
	gremlinc_writer_dispose(&w);
	gremlind_arena_free_malloc(&arena);
}

void message_test(void);

void message_test(void)
{
	RUN_TEST(emit_message_from_a_garbage_arena);
	RUN_TEST(predicate_does_not_take_an_enum_value_name);
	RUN_TEST(iter_predicate_does_not_take_a_getter_name);
}

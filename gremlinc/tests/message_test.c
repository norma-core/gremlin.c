#include <string.h>

#include "tests.h"
#include "gremlinc/lib.h"
#include "gremlind/lib.h"

/* An arena handed over full of garbage, as a caller's own buffer may be:
 * every field the naming passes have not run on must read as unset. */
static TEST(emit_message_without_reader_name_pass)
{
	char src[] =
		"syntax = \"proto3\";\n"
		"message A { B b = 1; }\n"
		"message B { A a = 1; bytes raw = 2; }\n";
	static char chunk[64 * 1024];
	memset(chunk, 0xa5, sizeof chunk);

	struct gremlind_arena arena;
	gremlind_arena_init(&arena, chunk, sizeof chunk, NULL, NULL);
	struct gremlind_source gs = { .path = "t.proto", .path_len = 7 };
	gremlinp_parser_buffer_init(&gs.buf, src, 0);
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, &gs, 1);
	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_scoped_names(&arena, ctx.files[0]));
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_visibility(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_resolve_type_refs(&ctx));
	struct gremlind_file *file = ctx.files[0];

	struct gremlinc_name_scope s;
	gremlinc_name_scope_init(&s, &arena);
	ASSERT_EQ(GREMLINP_OK, gremlinc_assign_c_names(&s, file));
	ASSERT_NULL(file->messages.items[0].c_reader_ok);

	struct gremlinc_writer w;
	ASSERT_TRUE(gremlinc_writer_init_owned(&w, 4096));
	ASSERT_EQ(GREMLINP_OK, gremlinc_emit_message(&w, &s, &file->messages.items[0]));
	ASSERT_TRUE(w.offset > 0);
	gremlinc_writer_dispose(&w);
}

void message_test(void);

void message_test(void)
{
	RUN_TEST(emit_message_without_reader_name_pass);
}

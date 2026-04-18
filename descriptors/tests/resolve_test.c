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

static void src_init(struct gremlind_source *s, const char *path, char *buf)
{
	s->path = path;
	s->path_len = strlen(path);
	gremlinp_parser_buffer_init(&s->buf, buf, 0);
}

static TEST(resolve_single_file_no_imports)
{
	char src[] = "syntax = \"proto3\";\n";
	struct gremlind_source sources[1];
	src_init(&sources[0], "only.proto", src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 1);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_NOT_NULL(ctx.files);
	ASSERT_NOT_NULL(ctx.files[0]);
	ASSERT_EQ(0, ctx.files[0]->imports.count);

	gremlind_arena_free_malloc(&arena);
}

static TEST(resolve_two_files_one_imports_the_other)
{
	char a_src[] =
		"syntax = \"proto3\";\n"
		"import \"b.proto\";\n";
	char b_src[] = "syntax = \"proto3\";\n";

	struct gremlind_source sources[2];
	src_init(&sources[0], "a.proto", a_src);
	src_init(&sources[1], "b.proto", b_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 2);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));

	ASSERT_EQ(1, ctx.files[0]->imports.count);
	ASSERT_NOT_NULL(ctx.files[0]->imports.items[0].resolved);
	ASSERT_TRUE(ctx.files[0]->imports.items[0].resolved == ctx.files[1]);

	gremlind_arena_free_malloc(&arena);
}

static TEST(resolve_nested_path_match)
{
	char a_src[] =
		"syntax = \"proto3\";\n"
		"import \"google/protobuf/timestamp.proto\";\n";
	char ts_src[] = "syntax = \"proto3\";\n";

	struct gremlind_source sources[2];
	src_init(&sources[0], "a.proto", a_src);
	src_init(&sources[1], "google/protobuf/timestamp.proto", ts_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 2);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_TRUE(ctx.files[0]->imports.items[0].resolved == ctx.files[1]);

	gremlind_arena_free_malloc(&arena);
}

static TEST(resolve_not_found_reports_failure)
{
	char a_src[] =
		"syntax = \"proto3\";\n"
		"import \"missing.proto\";\n";

	struct gremlind_source sources[1];
	src_init(&sources[0], "a.proto", a_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 1);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_ERROR_IMPORT_TARGET_NOT_FOUND, gremlind_link_imports(&ctx));
	ASSERT_EQ(0, ctx.failed_source_idx);
	ASSERT_NOT_NULL(ctx.failed_import);
	ASSERT_TRUE(ctx.failed_import == &ctx.files[0]->imports.items[0]);
	ASSERT_NULL(ctx.failed_import->resolved);

	gremlind_arena_free_malloc(&arena);
}

static TEST(resolve_self_import_detected_as_cycle)
{
	char a_src[] =
		"syntax = \"proto3\";\n"
		"import \"a.proto\";\n";

	struct gremlind_source sources[1];
	src_init(&sources[0], "a.proto", a_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 1);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_EQ(GREMLINP_ERROR_CIRCULAR_IMPORT, gremlind_check_no_cycles(&ctx));

	gremlind_arena_free_malloc(&arena);
}

static TEST(resolve_public_import_still_errors_on_cycle)
{
	/* protoc: public / weak don't relax cycle detection. */
	char a_src[] =
		"syntax = \"proto3\";\n"
		"import public \"b.proto\";\n";
	char b_src[] =
		"syntax = \"proto3\";\n"
		"import \"a.proto\";\n";

	struct gremlind_source sources[2];
	src_init(&sources[0], "a.proto", a_src);
	src_init(&sources[1], "b.proto", b_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 2);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_EQ(GREMLINP_ERROR_CIRCULAR_IMPORT, gremlind_check_no_cycles(&ctx));

	gremlind_arena_free_malloc(&arena);
}

static TEST(resolve_mutual_imports_link_but_cycle_detected)
{
	char a_src[] =
		"syntax = \"proto3\";\n"
		"import \"b.proto\";\n";
	char b_src[] =
		"syntax = \"proto3\";\n"
		"import \"a.proto\";\n";

	struct gremlind_source sources[2];
	src_init(&sources[0], "a.proto", a_src);
	src_init(&sources[1], "b.proto", b_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 2);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_TRUE(ctx.files[0]->imports.items[0].resolved == ctx.files[1]);
	ASSERT_TRUE(ctx.files[1]->imports.items[0].resolved == ctx.files[0]);

	/* link succeeds; the cycle detector is the gatekeeper. */
	ASSERT_EQ(GREMLINP_ERROR_CIRCULAR_IMPORT, gremlind_check_no_cycles(&ctx));

	gremlind_arena_free_malloc(&arena);
}

static TEST(resolve_three_file_cycle_detected)
{
	char a_src[] =
		"syntax = \"proto3\";\n"
		"import \"b.proto\";\n";
	char b_src[] =
		"syntax = \"proto3\";\n"
		"import \"c.proto\";\n";
	char c_src[] =
		"syntax = \"proto3\";\n"
		"import \"a.proto\";\n";

	struct gremlind_source sources[3];
	src_init(&sources[0], "a.proto", a_src);
	src_init(&sources[1], "b.proto", b_src);
	src_init(&sources[2], "c.proto", c_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 3);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_EQ(GREMLINP_ERROR_CIRCULAR_IMPORT, gremlind_check_no_cycles(&ctx));

	gremlind_arena_free_malloc(&arena);
}

static TEST(resolve_acyclic_diamond_passes_cycle_check)
{
	/* a -> b, a -> c, b -> d, c -> d — no cycle. */
	char a_src[] =
		"syntax = \"proto3\";\n"
		"import \"b.proto\";\n"
		"import \"c.proto\";\n";
	char b_src[] =
		"syntax = \"proto3\";\n"
		"import \"d.proto\";\n";
	char c_src[] =
		"syntax = \"proto3\";\n"
		"import \"d.proto\";\n";
	char d_src[] = "syntax = \"proto3\";\n";

	struct gremlind_source sources[4];
	src_init(&sources[0], "a.proto", a_src);
	src_init(&sources[1], "b.proto", b_src);
	src_init(&sources[2], "c.proto", c_src);
	src_init(&sources[3], "d.proto", d_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 4);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_check_no_cycles(&ctx));

	gremlind_arena_free_malloc(&arena);
}

/* ------------------------------------------------------------------------
 * visibility
 * ------------------------------------------------------------------------ */

static bool
visible_contains(struct gremlind_file *f, struct gremlind_file *target)
{
	for (size_t i = 0; i < f->visible.count; i++) {
		if (f->visible.items[i] == target) return true;
	}
	return false;
}

static TEST(visibility_no_imports_is_just_self)
{
	char src[] = "syntax = \"proto3\";\n";
	struct gremlind_source sources[1];
	src_init(&sources[0], "a.proto", src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 1);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_visibility(&ctx));

	ASSERT_EQ(1, ctx.files[0]->visible.count);
	ASSERT_TRUE(ctx.files[0]->visible.items[0] == ctx.files[0]);

	gremlind_arena_free_malloc(&arena);
}

static TEST(visibility_direct_regular_import_included)
{
	char a_src[] = "syntax = \"proto3\";\n"
		       "import \"b.proto\";\n";
	char b_src[] = "syntax = \"proto3\";\n";

	struct gremlind_source sources[2];
	src_init(&sources[0], "a.proto", a_src);
	src_init(&sources[1], "b.proto", b_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 2);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_visibility(&ctx));

	ASSERT_EQ(2, ctx.files[0]->visible.count);
	ASSERT_TRUE(visible_contains(ctx.files[0], ctx.files[0]));
	ASSERT_TRUE(visible_contains(ctx.files[0], ctx.files[1]));

	/* B doesn't see A (one-way). */
	ASSERT_EQ(1, ctx.files[1]->visible.count);
	ASSERT_FALSE(visible_contains(ctx.files[1], ctx.files[0]));

	gremlind_arena_free_malloc(&arena);
}

static TEST(visibility_regular_does_not_propagate_regular)
{
	/* A imports B; B imports (regular) C. A should NOT see C. */
	char a_src[] = "syntax = \"proto3\";\n"
		       "import \"b.proto\";\n";
	char b_src[] = "syntax = \"proto3\";\n"
		       "import \"c.proto\";\n";
	char c_src[] = "syntax = \"proto3\";\n";

	struct gremlind_source sources[3];
	src_init(&sources[0], "a.proto", a_src);
	src_init(&sources[1], "b.proto", b_src);
	src_init(&sources[2], "c.proto", c_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 3);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_visibility(&ctx));

	/* A.visible = {A, B} — C is NOT visible through a regular import. */
	ASSERT_EQ(2, ctx.files[0]->visible.count);
	ASSERT_TRUE(visible_contains(ctx.files[0], ctx.files[1]));
	ASSERT_FALSE(visible_contains(ctx.files[0], ctx.files[2]));

	gremlind_arena_free_malloc(&arena);
}

static TEST(visibility_regular_propagates_public)
{
	/* A imports B; B imports public C. A should see C. */
	char a_src[] = "syntax = \"proto3\";\n"
		       "import \"b.proto\";\n";
	char b_src[] = "syntax = \"proto3\";\n"
		       "import public \"c.proto\";\n";
	char c_src[] = "syntax = \"proto3\";\n";

	struct gremlind_source sources[3];
	src_init(&sources[0], "a.proto", a_src);
	src_init(&sources[1], "b.proto", b_src);
	src_init(&sources[2], "c.proto", c_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 3);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_visibility(&ctx));

	ASSERT_EQ(3, ctx.files[0]->visible.count);
	ASSERT_TRUE(visible_contains(ctx.files[0], ctx.files[2]));

	gremlind_arena_free_malloc(&arena);
}

static TEST(visibility_chain_of_publics_is_fully_transitive)
{
	/* A imports B; B imports public C; C imports public D. A sees all. */
	char a_src[] = "syntax = \"proto3\";\n"
		       "import \"b.proto\";\n";
	char b_src[] = "syntax = \"proto3\";\n"
		       "import public \"c.proto\";\n";
	char c_src[] = "syntax = \"proto3\";\n"
		       "import public \"d.proto\";\n";
	char d_src[] = "syntax = \"proto3\";\n";

	struct gremlind_source sources[4];
	src_init(&sources[0], "a.proto", a_src);
	src_init(&sources[1], "b.proto", b_src);
	src_init(&sources[2], "c.proto", c_src);
	src_init(&sources[3], "d.proto", d_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 4);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_visibility(&ctx));

	ASSERT_EQ(4, ctx.files[0]->visible.count);
	ASSERT_TRUE(visible_contains(ctx.files[0], ctx.files[2]));
	ASSERT_TRUE(visible_contains(ctx.files[0], ctx.files[3]));

	gremlind_arena_free_malloc(&arena);
}

static TEST(visibility_mixed_private_blocks_further_propagation)
{
	/* A imports B; B imports public C; C imports (regular) D.
	 * A sees {A, B, C} but not D — the C→D edge isn't public. */
	char a_src[] = "syntax = \"proto3\";\n"
		       "import \"b.proto\";\n";
	char b_src[] = "syntax = \"proto3\";\n"
		       "import public \"c.proto\";\n";
	char c_src[] = "syntax = \"proto3\";\n"
		       "import \"d.proto\";\n";
	char d_src[] = "syntax = \"proto3\";\n";

	struct gremlind_source sources[4];
	src_init(&sources[0], "a.proto", a_src);
	src_init(&sources[1], "b.proto", b_src);
	src_init(&sources[2], "c.proto", c_src);
	src_init(&sources[3], "d.proto", d_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 4);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_visibility(&ctx));

	ASSERT_EQ(3, ctx.files[0]->visible.count);
	ASSERT_TRUE(visible_contains(ctx.files[0], ctx.files[1]));
	ASSERT_TRUE(visible_contains(ctx.files[0], ctx.files[2]));
	ASSERT_FALSE(visible_contains(ctx.files[0], ctx.files[3]));

	gremlind_arena_free_malloc(&arena);
}

/* ------------------------------------------------------------------------
 * topological ordering
 * ------------------------------------------------------------------------ */

static size_t
find_in_msg_order(const struct gremlind_message_order *o,
		  const struct gremlind_message *m)
{
	for (size_t i = 0; i < o->count; i++) {
		if (o->items[i] == m) return i;
	}
	return (size_t)-1;
}

static bool
predeclare_contains(const struct gremlind_message_order *o,
		    const struct gremlind_message *m)
{
	for (size_t i = 0; i < o->predeclare_count; i++) {
		if (o->predeclare[i] == m) return true;
	}
	return false;
}

static void
full_pipeline(struct gremlind_resolve_context *ctx, struct gremlind_arena *arena)
{
	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(ctx));
	for (size_t i = 0; i < ctx->n_sources; i++) {
		ASSERT_EQ(GREMLINP_OK, gremlind_compute_scoped_names(arena, ctx->files[i]));
	}
	ASSERT_EQ(GREMLINP_OK, gremlind_compute_visibility(ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_resolve_type_refs(ctx));
}

static TEST(topo_files_linear_chain)
{
	/* a imports b; b imports c. Expected emission order: c, b, a. */
	char a_src[] = "syntax = \"proto3\";\n"
		       "import \"b.proto\";\n";
	char b_src[] = "syntax = \"proto3\";\n"
		       "import \"c.proto\";\n";
	char c_src[] = "syntax = \"proto3\";\n";

	struct gremlind_source sources[3];
	src_init(&sources[0], "a.proto", a_src);
	src_init(&sources[1], "b.proto", b_src);
	src_init(&sources[2], "c.proto", c_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 3);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(&ctx));

	struct gremlind_file_order order;
	ASSERT_EQ(GREMLINP_OK, gremlind_topo_sort_files(&ctx, &order));
	ASSERT_EQ(3, order.count);
	ASSERT_TRUE(order.items[0] == ctx.files[2]);   /* c first */
	ASSERT_TRUE(order.items[1] == ctx.files[1]);   /* then b */
	ASSERT_TRUE(order.items[2] == ctx.files[0]);   /* a last */

	gremlind_arena_free_malloc(&arena);
}

static TEST(topo_messages_field_deps_ordered_no_predeclare)
{
	/* A has B, B has only a builtin. Expect [B, A], empty predeclare. */
	char src[] =
		"syntax = \"proto3\";\n"
		"message A { B b = 1; }\n"
		"message B { int32 n = 1; }\n";

	struct gremlind_source sources[1];
	src_init(&sources[0], "x.proto", src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 1);
	full_pipeline(&ctx, &arena);

	struct gremlind_message_order order;
	ASSERT_EQ(GREMLINP_OK, gremlind_topo_sort_messages(&arena, ctx.files[0], &order));

	ASSERT_EQ(2, order.count);
	ASSERT_EQ(0, order.predeclare_count);

	size_t ia = find_in_msg_order(&order, &ctx.files[0]->messages.items[0]);
	size_t ib = find_in_msg_order(&order, &ctx.files[0]->messages.items[1]);
	ASSERT_TRUE(ib < ia);   /* B before A */

	gremlind_arena_free_malloc(&arena);
}

static TEST(topo_messages_self_ref_predeclares_self)
{
	/* message M { M next = 1; } — M must predeclare itself. */
	char src[] =
		"syntax = \"proto3\";\n"
		"message M { M next = 1; int32 v = 2; }\n";

	struct gremlind_source sources[1];
	src_init(&sources[0], "x.proto", src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 1);
	full_pipeline(&ctx, &arena);

	struct gremlind_message_order order;
	ASSERT_EQ(GREMLINP_OK, gremlind_topo_sort_messages(&arena, ctx.files[0], &order));

	ASSERT_EQ(1, order.count);
	ASSERT_EQ(1, order.predeclare_count);
	ASSERT_TRUE(order.predeclare[0] == &ctx.files[0]->messages.items[0]);

	gremlind_arena_free_malloc(&arena);
}

static TEST(topo_messages_mutual_ref_predeclares_cycle_member)
{
	/* A has B, B has A. DFS from A enters B, B hits A (GRAY) → predeclare
	 * A. Output has 2 messages, predeclare list has exactly 1. */
	char src[] =
		"syntax = \"proto3\";\n"
		"message A { B b = 1; }\n"
		"message B { A a = 1; }\n";

	struct gremlind_source sources[1];
	src_init(&sources[0], "x.proto", src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 1);
	full_pipeline(&ctx, &arena);

	struct gremlind_message_order order;
	ASSERT_EQ(GREMLINP_OK, gremlind_topo_sort_messages(&arena, ctx.files[0], &order));

	ASSERT_EQ(2, order.count);
	ASSERT_EQ(1, order.predeclare_count);
	/* A is message[0]; it's the cycle's GRAY target when B tries to
	 * descend into it, so it's the one that needs predeclaration. */
	ASSERT_TRUE(predeclare_contains(&order, &ctx.files[0]->messages.items[0]));

	gremlind_arena_free_malloc(&arena);
}

static TEST(resolve_parse_error_reports_source_idx)
{
	char a_src[] = "syntax = \"proto3\";\n";
	char b_src[] = "syntax = \"proto9\";\n";

	struct gremlind_source sources[2];
	src_init(&sources[0], "a.proto", a_src);
	src_init(&sources[1], "b.proto", b_src);

	struct gremlind_arena arena = make_arena();
	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 2);

	ASSERT_TRUE(gremlind_build_all(&ctx) != GREMLINP_OK);
	ASSERT_EQ(1, ctx.failed_source_idx);

	gremlind_arena_free_malloc(&arena);
}

void resolve_test(void);

void resolve_test(void)
{
	RUN_TEST(resolve_single_file_no_imports);
	RUN_TEST(resolve_two_files_one_imports_the_other);
	RUN_TEST(resolve_nested_path_match);
	RUN_TEST(resolve_not_found_reports_failure);
	RUN_TEST(resolve_self_import_detected_as_cycle);
	RUN_TEST(resolve_public_import_still_errors_on_cycle);
	RUN_TEST(resolve_mutual_imports_link_but_cycle_detected);
	RUN_TEST(resolve_three_file_cycle_detected);
	RUN_TEST(resolve_acyclic_diamond_passes_cycle_check);
	RUN_TEST(visibility_no_imports_is_just_self);
	RUN_TEST(visibility_direct_regular_import_included);
	RUN_TEST(visibility_regular_does_not_propagate_regular);
	RUN_TEST(visibility_regular_propagates_public);
	RUN_TEST(visibility_chain_of_publics_is_fully_transitive);
	RUN_TEST(visibility_mixed_private_blocks_further_propagation);
	RUN_TEST(topo_files_linear_chain);
	RUN_TEST(topo_messages_field_deps_ordered_no_predeclare);
	RUN_TEST(topo_messages_self_ref_predeclares_self);
	RUN_TEST(topo_messages_mutual_ref_predeclares_cycle_member);
	RUN_TEST(resolve_parse_error_reports_source_idx);
}

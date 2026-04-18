#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests.h"
#include "gremlind/lib.h"

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

// Integration test: drive the full gremlind pipeline on a corpus of real
// protobuf test files. Sources come from two places:
//   - descriptors/tests/proto_corpus/*.proto — gremlin.zig's test_data/google
//     (proto2/proto3 test suite), flat because imports use bare filenames.
//   - descriptors/tests/proto_corpus/google/protobuf/*.proto — canonical
//     Google WKTs, under the path they're imported by (google/protobuf/any.proto etc).
// The logical path we register each file under matches what `import "..."`
// statements reference — so lookups are plain byte-equal span matches.

static char *
slurp(const char *relative)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/proto_corpus/%s", TEST_DATA_DIR, relative);

	FILE *f = fopen(path, "r");
	if (f == NULL) {
		fprintf(stderr, "slurp: cannot open %s\n", path);
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc((size_t)sz + 1);
	if (buf == NULL) { fclose(f); return NULL; }
	size_t got = fread(buf, 1, (size_t)sz, f);
	buf[got] = '\0';
	fclose(f);
	return buf;
}

struct suite {
	const char *logical;
	char *buffer;
};

static void
suite_free(struct suite *s, size_t n)
{
	for (size_t i = 0; i < n; i++) free(s[i].buffer);
}

static void
build_sources(struct suite *s, size_t n, struct gremlind_source *out)
{
	for (size_t i = 0; i < n; i++) {
		out[i].path = s[i].logical;
		out[i].path_len = strlen(s[i].logical);
		gremlinp_parser_buffer_init(&out[i].buf, s[i].buffer, 0);
	}
}

static void
run_full_pipeline(struct gremlind_arena *arena,
		  struct gremlind_resolve_context *ctx,
		  struct gremlind_source *sources, size_t n)
{
	gremlind_resolve_context_init(ctx, arena, sources, n);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_link_imports(ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_check_no_cycles(ctx));

	for (size_t i = 0; i < n; i++) {
		ASSERT_EQ(GREMLINP_OK,
			gremlind_compute_scoped_names(arena, ctx->files[i]));
	}

	ASSERT_EQ(GREMLINP_OK, gremlind_compute_visibility(ctx));
	ASSERT_EQ(GREMLINP_OK, gremlind_resolve_type_refs(ctx));
}

/* ------------------------------------------------------------------------
 * benchmark_message3 suite — 9 proto2 files, rich cross-file imports, all
 * in the same package. Stress-tests the multi-file pipeline on real
 * Google protobuf corpus.
 * ------------------------------------------------------------------------ */

static TEST(integration_benchmark_message3_suite)
{
	static const char *names[] = {
		"benchmark_message3.proto",
		"benchmark_message3_1.proto",
		"benchmark_message3_2.proto",
		"benchmark_message3_3.proto",
		"benchmark_message3_4.proto",
		"benchmark_message3_5.proto",
		"benchmark_message3_6.proto",
		"benchmark_message3_7.proto",
		"benchmark_message3_8.proto",
	};
	const size_t N = sizeof(names) / sizeof(names[0]);

	struct suite files[9];
	for (size_t i = 0; i < N; i++) {
		files[i].logical = names[i];
		files[i].buffer = slurp(names[i]);
		ASSERT_NOT_NULL(files[i].buffer);
	}

	struct gremlind_source sources[9];
	build_sources(files, N, sources);

	struct gremlind_arena arena;
	ASSERT_TRUE(gremlind_arena_init_malloc(&arena, 1u << 20));

	struct gremlind_resolve_context ctx;
	run_full_pipeline(&arena, &ctx, sources, N);

	/* benchmark_message3.proto imports _1, _2, _3, _4, _5, _7, _8
	 * (sees 8 files total via direct imports). */
	ASSERT_EQ(8, ctx.files[0]->visible.count);

	/* The family uses package benchmarks.google_message3 — every top-level
	 * message should have that 3-seg prefix in its scoped_name. */
	for (size_t i = 0; i < N; i++) {
		struct gremlind_file *f = ctx.files[i];
		for (size_t m = 0; m < f->messages.count; m++) {
			struct gremlind_message *msg = &f->messages.items[m];
			ASSERT_TRUE(msg->scoped_name.n_segments >= 3);
			ASSERT_SPAN_EQ("benchmarks",
				msg->scoped_name.segments[0].start,
				msg->scoped_name.segments[0].length);
			ASSERT_SPAN_EQ("google_message3",
				msg->scoped_name.segments[1].start,
				msg->scoped_name.segments[1].length);
		}
	}

	gremlind_arena_free_malloc(&arena);
	suite_free(files, N);
}

/* ------------------------------------------------------------------------
 * unittest suite — 3 proto2 files, public import chain.
 *   unittest.proto  ──► unittest_import.proto
 *   unittest_import.proto  ── public ──► unittest_import_public.proto
 * unittest.proto should therefore see all three via visibility.
 * ------------------------------------------------------------------------ */

static TEST(integration_unittest_public_import_visibility)
{
	static const char *names[] = {
		"unittest.proto",
		"unittest_import.proto",
		"unittest_import_public.proto",
	};
	const size_t N = sizeof(names) / sizeof(names[0]);

	struct suite files[3];
	for (size_t i = 0; i < N; i++) {
		files[i].logical = names[i];
		files[i].buffer = slurp(names[i]);
		ASSERT_NOT_NULL(files[i].buffer);
	}

	struct gremlind_source sources[3];
	build_sources(files, N, sources);

	struct gremlind_arena arena;
	ASSERT_TRUE(gremlind_arena_init_malloc(&arena, 1u << 20));

	struct gremlind_resolve_context ctx;
	run_full_pipeline(&arena, &ctx, sources, N);

	/* unittest.proto should see all 3 files through the public chain. */
	struct gremlind_file *ut = ctx.files[0];
	ASSERT_EQ(3, ut->visible.count);

	/* unittest_import_public.proto is reachable from unittest.proto only
	 * via unittest_import.proto's public re-export. */
	bool saw_public = false;
	for (size_t i = 0; i < ut->visible.count; i++) {
		if (ut->visible.items[i] == ctx.files[2]) {
			saw_public = true;
			break;
		}
	}
	ASSERT_TRUE(saw_public);

	gremlind_arena_free_malloc(&arena);
	suite_free(files, N);
}

/* ------------------------------------------------------------------------
 * Single-file proto3 with map fields — benchmark.proto has none, but
 * map_test.proto exercises map<string, ...>, map<int32, ...>, and nested
 * messages. Good smoke test that the field parser + build pipeline
 * handles map fields (which our node layer currently ignores beyond the
 * parser result, but the parser still has to grok them).
 * ------------------------------------------------------------------------ */

static TEST(integration_map_test_proto3)
{
	struct suite files[1];
	files[0].logical = "map_test.proto";
	files[0].buffer = slurp("map_test.proto");
	ASSERT_NOT_NULL(files[0].buffer);

	struct gremlind_source sources[1];
	build_sources(files, 1, sources);

	struct gremlind_arena arena;
	ASSERT_TRUE(gremlind_arena_init_malloc(&arena, 1u << 18));

	struct gremlind_resolve_context ctx;
	run_full_pipeline(&arena, &ctx, sources, 1);

	/* Has top-level package map_test and at least one message. */
	ASSERT_TRUE(ctx.files[0]->package.present);
	ASSERT_SPAN_EQ("map_test",
		ctx.files[0]->package.value.name_start,
		ctx.files[0]->package.value.name_length);
	ASSERT_TRUE(ctx.files[0]->messages.count > 0);

	gremlind_arena_free_malloc(&arena);
	suite_free(files, 1);
}

/* ------------------------------------------------------------------------
 * Missing-import case: load proto3.proto alone, without its WKTs.
 * link_imports must report IMPORT_TARGET_NOT_FOUND with the offending
 * import surfaced so callers can produce a good error message.
 * ------------------------------------------------------------------------ */

static TEST(integration_missing_wkt_import_reported)
{
	struct suite files[1];
	files[0].logical = "proto3.proto";
	files[0].buffer = slurp("proto3.proto");
	ASSERT_NOT_NULL(files[0].buffer);

	struct gremlind_source sources[1];
	build_sources(files, 1, sources);

	struct gremlind_arena arena;
	ASSERT_TRUE(gremlind_arena_init_malloc(&arena, 1u << 18));

	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, &arena, sources, 1);

	ASSERT_EQ(GREMLINP_OK, gremlind_build_all(&ctx));
	ASSERT_EQ(GREMLINP_ERROR_IMPORT_TARGET_NOT_FOUND,
		  gremlind_link_imports(&ctx));
	ASSERT_NOT_NULL(ctx.failed_import);
	ASSERT_SPAN_EQ("google/protobuf/any.proto",
		ctx.failed_import->parsed.path_start,
		ctx.failed_import->parsed.path_length);

	gremlind_arena_free_malloc(&arena);
	suite_free(files, 1);
}

// proto3.proto + WKTs — the full pipeline end-to-end on a proto3 file
// with real google/protobuf/X.proto dependencies. proto3.proto imports six
// WKTs directly (any, duration, field_mask, struct, timestamp, wrappers),
// all self-contained (no further deps among this set).

static TEST(integration_proto3_with_wkts_resolves)
{
	static const char *names[] = {
		"proto3.proto",
		"google/protobuf/any.proto",
		"google/protobuf/duration.proto",
		"google/protobuf/field_mask.proto",
		"google/protobuf/struct.proto",
		"google/protobuf/timestamp.proto",
		"google/protobuf/wrappers.proto",
	};
	const size_t N = sizeof(names) / sizeof(names[0]);

	struct suite files[7];
	for (size_t i = 0; i < N; i++) {
		files[i].logical = names[i];
		files[i].buffer = slurp(names[i]);
		ASSERT_NOT_NULL(files[i].buffer);
	}

	struct gremlind_source sources[7];
	build_sources(files, N, sources);

	struct gremlind_arena arena;
	ASSERT_TRUE(gremlind_arena_init_malloc(&arena, 1u << 20));

	struct gremlind_resolve_context ctx;
	run_full_pipeline(&arena, &ctx, sources, N);

	/* proto3.proto has 6 direct imports, all regular → visible = {self, 6 WKTs}. */
	ASSERT_EQ(7, ctx.files[0]->visible.count);

	/* Every import in proto3.proto must now point at one of the WKT
	 * files in our corpus. */
	ASSERT_EQ(6, ctx.files[0]->imports.count);
	for (size_t k = 0; k < ctx.files[0]->imports.count; k++) {
		ASSERT_NOT_NULL(ctx.files[0]->imports.items[k].resolved);
	}

	/* Spot-check WKT package: google.protobuf */
	struct gremlind_file *any = ctx.files[1];
	ASSERT_TRUE(any->package.present);
	ASSERT_SPAN_EQ("google.protobuf",
		any->package.value.name_start,
		any->package.value.name_length);

	gremlind_arena_free_malloc(&arena);
	suite_free(files, N);
}

/* ------------------------------------------------------------------------
 * Kitchen-sink — load the entire corpus as one batch: all three families
 * + all WKTs. 26 files, multiple packages, regular + public imports, a
 * mix of proto2 and proto3. Smoke test that nothing about file ordering,
 * package separation, or cross-suite visibility misbehaves.
 * ------------------------------------------------------------------------ */

static TEST(integration_full_corpus_pipeline)
{
	static const char *names[] = {
		/* gremlin.zig family */
		"benchmark.proto",
		"benchmark_message3.proto",
		"benchmark_message3_1.proto",
		"benchmark_message3_2.proto",
		"benchmark_message3_3.proto",
		"benchmark_message3_4.proto",
		"benchmark_message3_5.proto",
		"benchmark_message3_6.proto",
		"benchmark_message3_7.proto",
		"benchmark_message3_8.proto",
		"map_test.proto",
		"unittest.proto",
		"unittest_import.proto",
		"unittest_import_public.proto",
		/* proto3 + its WKTs */
		"proto3.proto",
		"google/protobuf/any.proto",
		"google/protobuf/api.proto",
		"google/protobuf/descriptor.proto",
		"google/protobuf/duration.proto",
		"google/protobuf/empty.proto",
		"google/protobuf/field_mask.proto",
		"google/protobuf/source_context.proto",
		"google/protobuf/struct.proto",
		"google/protobuf/timestamp.proto",
		"google/protobuf/type.proto",
		"google/protobuf/wrappers.proto",
	};
	const size_t N = sizeof(names) / sizeof(names[0]);

	struct suite files[26];
	for (size_t i = 0; i < N; i++) {
		files[i].logical = names[i];
		files[i].buffer = slurp(names[i]);
		ASSERT_NOT_NULL(files[i].buffer);
	}

	struct gremlind_source sources[26];
	build_sources(files, N, sources);

	struct gremlind_arena arena;
	ASSERT_TRUE(gremlind_arena_init_malloc(&arena, 1u << 22));

	struct gremlind_resolve_context ctx;
	run_full_pipeline(&arena, &ctx, sources, N);

	/* Every file built → no nulls. */
	for (size_t i = 0; i < N; i++) {
		ASSERT_NOT_NULL(ctx.files[i]);
	}

	/* Every import must be resolved. */
	for (size_t i = 0; i < N; i++) {
		struct gremlind_file *f = ctx.files[i];
		for (size_t k = 0; k < f->imports.count; k++) {
			ASSERT_NOT_NULL(f->imports.items[k].resolved);
		}
	}

	gremlind_arena_free_malloc(&arena);
	suite_free(files, N);
}

// Every .proto in our corpus. Used by the kitchen-sink test (all loaded
// at once and cross-resolved) and by the standalone builder (each loaded
// in isolation, verifying the parser + build pass works on the file on
// its own, regardless of whether its imports are provided).
static const char *ALL_PROTOS[] = {
	// gremlin.zig suite
	"benchmark.proto",
	"benchmark_message3.proto",
	"benchmark_message3_1.proto",
	"benchmark_message3_2.proto",
	"benchmark_message3_3.proto",
	"benchmark_message3_4.proto",
	"benchmark_message3_5.proto",
	"benchmark_message3_6.proto",
	"benchmark_message3_7.proto",
	"benchmark_message3_8.proto",
	"map_test.proto",
	"unittest.proto",
	"unittest_import.proto",
	"unittest_import_public.proto",
	"whatsapp.proto",
	"gogofast.proto",
	// proto3 + WKTs
	"proto3.proto",
	"google/protobuf/any.proto",
	"google/protobuf/api.proto",
	"google/protobuf/descriptor.proto",
	"google/protobuf/duration.proto",
	"google/protobuf/empty.proto",
	"google/protobuf/field_mask.proto",
	"google/protobuf/source_context.proto",
	"google/protobuf/struct.proto",
	"google/protobuf/timestamp.proto",
	"google/protobuf/type.proto",
	"google/protobuf/wrappers.proto",
};
#define N_ALL_PROTOS (sizeof(ALL_PROTOS) / sizeof(ALL_PROTOS[0]))

// Build every proto in the corpus *in isolation* — no cross-file resolution,
// no imports required to be present. Stresses the parser + build pass
// (gremlind_build_file, not the full pipeline) across ~28 real Google /
// community protos totalling ~15.8k lines of source.
static TEST(integration_every_proto_builds_standalone)
{
	for (size_t i = 0; i < N_ALL_PROTOS; i++) {
		char *src = slurp(ALL_PROTOS[i]);
		ASSERT_NOT_NULL(src);

		struct gremlind_arena arena;
		ASSERT_TRUE(gremlind_arena_init_malloc(&arena, 1u << 18));

		struct gremlinp_parser_buffer buf;
		gremlinp_parser_buffer_init(&buf, src, 0);
		struct gremlind_build_result r = gremlind_build_file(&arena, &buf);

		if (r.error != GREMLINP_OK) {
			fprintf(stderr,
				"\nbuild failed on %s: error=%d at offset %zu\n",
				ALL_PROTOS[i], (int)r.error, r.error_offset);
		}
		ASSERT_EQ(GREMLINP_OK, r.error);
		ASSERT_NOT_NULL(r.file);

		gremlind_arena_free_malloc(&arena);
		free(src);
	}
}

void integration_test(void);

void integration_test(void)
{
	RUN_TEST(integration_benchmark_message3_suite);
	RUN_TEST(integration_unittest_public_import_visibility);
	RUN_TEST(integration_map_test_proto3);
	RUN_TEST(integration_missing_wkt_import_reported);
	RUN_TEST(integration_proto3_with_wkts_resolves);
	RUN_TEST(integration_full_corpus_pipeline);
	RUN_TEST(integration_every_proto_builds_standalone);
}

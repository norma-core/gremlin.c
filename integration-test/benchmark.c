#define _GNU_SOURCE
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gremlin.h"
#include "benchmark.pb.h"
#include "unittest.pb.h"
#include "unittest_import.pb.h"
#include "unittest_import_public.pb.h"

#include "test_framework.h"	/* test_load_binary */

/*
 * 1:1 port of gremlin.zig/integration-test/benchmark.zig.
 *
 * Two workloads × four benchmarks each:
 *   DeepNested (a 5-level nested / repeated tree) — Marshal, Unmarshal,
 *                                                    LazyRead, DeepAccess.
 *   Golden TestAllTypes (protobuf_unittest, binaries/golden_message) —
 *                                      same four benchmarks.
 *
 * Matches zig's output format line-for-line so external dashboards
 * can diff the two runs directly.
 */

/* ========================================================================
 * Golden Message construction (matches createGoldenMessage in zig)
 * ======================================================================== */

/* All sub-messages + repeated storage live at file scope so the top-level
 * Golden struct's pointers stay valid for the lifetime of the program. */
static protobuf_unittest_TestAllTypes_NestedMessage        g_nested_118 = { .bb = 118 };
static protobuf_unittest_ForeignMessage                    g_foreign_119 = { .c = 119 };
static protobuf_unittest_import_ImportMessage              g_import_120 = { .d = 120 };
static protobuf_unittest_import_PublicImportMessage        g_public_126 = { .e = 126 };
static protobuf_unittest_TestAllTypes_NestedMessage        g_lazy_127 = { .bb = 127 };
static protobuf_unittest_TestAllTypes_NestedMessage        g_ulazy_128 = { .bb = 128 };

static int32_t  g_rep_int32[]    = { 201, 301 };
static int64_t  g_rep_int64[]    = { 202, 302 };
static uint32_t g_rep_uint32[]   = { 203, 303 };
static uint64_t g_rep_uint64[]   = { 204, 304 };
static int32_t  g_rep_sint32[]   = { 205, 305 };
static int64_t  g_rep_sint64[]   = { 206, 306 };
static uint32_t g_rep_fixed32[]  = { 207, 307 };
static uint64_t g_rep_fixed64[]  = { 208, 308 };
static int32_t  g_rep_sfixed32[] = { 209, 309 };
static int64_t  g_rep_sfixed64[] = { 210, 310 };
static float    g_rep_float[]    = { 211, 311 };
static double   g_rep_double[]   = { 212, 312 };
static bool     g_rep_bool[]     = { true, false };

static protobuf_unittest_TestAllTypes_NestedMessage g_rep_nested_0 = { .bb = 218 };
static protobuf_unittest_TestAllTypes_NestedMessage g_rep_nested_1 = { .bb = 318 };
static const protobuf_unittest_TestAllTypes_NestedMessage *g_rep_nested[] = {
	&g_rep_nested_0, &g_rep_nested_1,
};

static protobuf_unittest_ForeignMessage g_rep_foreign_0 = { .c = 219 };
static protobuf_unittest_ForeignMessage g_rep_foreign_1 = { .c = 319 };
static const protobuf_unittest_ForeignMessage *g_rep_foreign[] = {
	&g_rep_foreign_0, &g_rep_foreign_1,
};

static protobuf_unittest_import_ImportMessage g_rep_import_0 = { .d = 220 };
static protobuf_unittest_import_ImportMessage g_rep_import_1 = { .d = 320 };
static const protobuf_unittest_import_ImportMessage *g_rep_import[] = {
	&g_rep_import_0, &g_rep_import_1,
};

static protobuf_unittest_TestAllTypes_NestedMessage g_rep_lazy_0 = { .bb = 227 };
static protobuf_unittest_TestAllTypes_NestedMessage g_rep_lazy_1 = { .bb = 327 };
static const protobuf_unittest_TestAllTypes_NestedMessage *g_rep_lazy[] = {
	&g_rep_lazy_0, &g_rep_lazy_1,
};

static protobuf_unittest_TestAllTypes_NestedEnum g_rep_nested_enum[] = {
	protobuf_unittest_TestAllTypes_NestedEnum_BAR,
	protobuf_unittest_TestAllTypes_NestedEnum_BAZ,
};
static protobuf_unittest_ForeignEnum g_rep_foreign_enum[] = {
	protobuf_unittest_ForeignEnum_FOREIGN_BAR,
	protobuf_unittest_ForeignEnum_FOREIGN_BAZ,
};
static protobuf_unittest_import_ImportEnum g_rep_import_enum[] = {
	protobuf_unittest_import_ImportEnum_IMPORT_BAR,
	protobuf_unittest_import_ImportEnum_IMPORT_BAZ,
};

static struct gremlin_bytes g_rep_string[] = {
	{ .data = (const uint8_t *)"215", .len = 3 },
	{ .data = (const uint8_t *)"315", .len = 3 },
};
static struct gremlin_bytes g_rep_bytes[] = {
	{ .data = (const uint8_t *)"216", .len = 3 },
	{ .data = (const uint8_t *)"316", .len = 3 },
};
static struct gremlin_bytes g_rep_string_piece[] = {
	{ .data = (const uint8_t *)"224", .len = 3 },
	{ .data = (const uint8_t *)"324", .len = 3 },
};
static struct gremlin_bytes g_rep_cord[] = {
	{ .data = (const uint8_t *)"225", .len = 3 },
	{ .data = (const uint8_t *)"325", .len = 3 },
};

/* Mutable storage the update loop writes into — corresponds to zig's
 * `repeated_storage: []?NestedMessage`. */
static protobuf_unittest_TestAllTypes_NestedMessage g_update_rep[2] = {
	{ .bb = 218 }, { .bb = 318 },
};
static const protobuf_unittest_TestAllTypes_NestedMessage *g_update_rep_ptrs[2] = {
	&g_update_rep[0], &g_update_rep[1],
};

static protobuf_unittest_TestAllTypes
create_golden_message(void)
{
	protobuf_unittest_TestAllTypes m = {
		.optional_int32    = 101,
		.optional_int64    = 102,
		.optional_uint32   = 103,
		.optional_uint64   = 104,
		.optional_sint32   = 105,
		.optional_sint64   = 106,
		.optional_fixed32  = 107,
		.optional_fixed64  = 108,
		.optional_sfixed32 = 109,
		.optional_sfixed64 = 110,
		.optional_float    = 111.0f,
		.optional_double   = 112.0,
		.optional_bool     = true,
		.optional_string   = { .data = (const uint8_t *)"115", .len = 3 },
		.optional_bytes    = { .data = (const uint8_t *)"116", .len = 3 },
		.optional_nested_message          = &g_nested_118,
		.optional_foreign_message         = &g_foreign_119,
		.optional_import_message          = &g_import_120,
		.optional_public_import_message   = &g_public_126,
		.optional_lazy_message            = &g_lazy_127,
		.optional_unverified_lazy_message = &g_ulazy_128,
		.optional_nested_enum  = protobuf_unittest_TestAllTypes_NestedEnum_BAZ,
		.optional_foreign_enum = protobuf_unittest_ForeignEnum_FOREIGN_BAZ,
		.optional_import_enum  = protobuf_unittest_import_ImportEnum_IMPORT_BAZ,
		.optional_string_piece = { .data = (const uint8_t *)"124", .len = 3 },
		.optional_cord         = { .data = (const uint8_t *)"125", .len = 3 },

		.repeated_int32    = g_rep_int32,    .repeated_int32_count    = 2,
		.repeated_int64    = g_rep_int64,    .repeated_int64_count    = 2,
		.repeated_uint32   = g_rep_uint32,   .repeated_uint32_count   = 2,
		.repeated_uint64   = g_rep_uint64,   .repeated_uint64_count   = 2,
		.repeated_sint32   = g_rep_sint32,   .repeated_sint32_count   = 2,
		.repeated_sint64   = g_rep_sint64,   .repeated_sint64_count   = 2,
		.repeated_fixed32  = g_rep_fixed32,  .repeated_fixed32_count  = 2,
		.repeated_fixed64  = g_rep_fixed64,  .repeated_fixed64_count  = 2,
		.repeated_sfixed32 = g_rep_sfixed32, .repeated_sfixed32_count = 2,
		.repeated_sfixed64 = g_rep_sfixed64, .repeated_sfixed64_count = 2,
		.repeated_float    = g_rep_float,    .repeated_float_count    = 2,
		.repeated_double   = g_rep_double,   .repeated_double_count   = 2,
		.repeated_bool     = g_rep_bool,     .repeated_bool_count     = 2,
		.repeated_string        = g_rep_string,        .repeated_string_count        = 2,
		.repeated_bytes         = g_rep_bytes,         .repeated_bytes_count         = 2,
		.repeated_string_piece  = g_rep_string_piece,  .repeated_string_piece_count  = 2,
		.repeated_cord          = g_rep_cord,          .repeated_cord_count          = 2,
		.repeated_nested_message  = g_rep_nested,  .repeated_nested_message_count  = 2,
		.repeated_foreign_message = g_rep_foreign, .repeated_foreign_message_count = 2,
		.repeated_import_message  = g_rep_import,  .repeated_import_message_count  = 2,
		.repeated_lazy_message    = g_rep_lazy,    .repeated_lazy_message_count    = 2,
		.repeated_nested_enum  = g_rep_nested_enum,  .repeated_nested_enum_count  = 2,
		.repeated_foreign_enum = g_rep_foreign_enum, .repeated_foreign_enum_count = 2,
		.repeated_import_enum  = g_rep_import_enum,  .repeated_import_enum_count  = 2,

		.default_int32    = 401,
		.default_int64    = 402,
		.default_uint32   = 403,
		.default_uint64   = 404,
		.default_sint32   = 405,
		.default_sint64   = 406,
		.default_fixed32  = 407,
		.default_fixed64  = 408,
		.default_sfixed32 = 409,
		.default_sfixed64 = 410,
		.default_float    = 411.0f,
		.default_double   = 412.0,
		.default_bool     = false,
		.default_string   = { .data = (const uint8_t *)"415", .len = 3 },
		.default_bytes    = { .data = (const uint8_t *)"416", .len = 3 },
		.default_nested_enum  = protobuf_unittest_TestAllTypes_NestedEnum_FOO,
		.default_foreign_enum = protobuf_unittest_ForeignEnum_FOREIGN_FOO,
		.default_import_enum  = protobuf_unittest_import_ImportEnum_IMPORT_FOO,
		.default_string_piece = { .data = (const uint8_t *)"424", .len = 3 },
		.default_cord         = { .data = (const uint8_t *)"425", .len = 3 },

		.oneof_uint32 = 601,
	};
	return m;
}

static void
update_golden_message(protobuf_unittest_TestAllTypes *msg, int32_t i)
{
	/* Mirror the zig benchmark's hot update so codegen can't hoist
	 * the encode out of the loop. */
	msg->optional_int32 = 101 + i;
	g_nested_118.bb = 118 + i;
	g_update_rep[0].bb = 218 + i;
	g_update_rep[1].bb = 318 + i;
	msg->repeated_nested_message = g_update_rep_ptrs;
	msg->repeated_nested_message_count = 2;
	msg->oneof_uint32 = (uint32_t)(601 + i);
}

/* ========================================================================
 * Deep Nested construction (matches createDeepNested in zig)
 * ======================================================================== */

static int32_t g_dn_lvl4_numbers_main[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

/* level4 items[0..4] */
static int32_t g_dn_l4_nums_0[] = { 10, 20, 30, 40, 50 };
static int32_t g_dn_l4_nums_1[] = { 60, 70, 80, 90, 100 };
static int32_t g_dn_l4_nums_2[] = { 110, 120, 130, 140, 150 };
static int32_t g_dn_l4_nums_3[] = { 160, 170, 180, 190, 200 };
static int32_t g_dn_l4_nums_4[] = { 210, 220, 230, 240, 250 };

static benchmark_Level4 g_dn_items_level4_0 = {
	.value = 41, .data = { .data = (const uint8_t *)"item1_with_numbers", .len = 18 },
	.numbers = g_dn_l4_nums_0, .numbers_count = 5
};
static benchmark_Level4 g_dn_items_level4_1 = {
	.value = 42, .data = { .data = (const uint8_t *)"item2_with_numbers", .len = 18 },
	.numbers = g_dn_l4_nums_1, .numbers_count = 5
};
static benchmark_Level4 g_dn_items_level4_2 = {
	.value = 43, .data = { .data = (const uint8_t *)"item3_with_numbers", .len = 18 },
	.numbers = g_dn_l4_nums_2, .numbers_count = 5
};
static benchmark_Level4 g_dn_items_level4_3 = {
	.value = 44, .data = { .data = (const uint8_t *)"item4_with_numbers", .len = 18 },
	.numbers = g_dn_l4_nums_3, .numbers_count = 5
};
static benchmark_Level4 g_dn_items_level4_4 = {
	.value = 45, .data = { .data = (const uint8_t *)"item5_with_numbers", .len = 18 },
	.numbers = g_dn_l4_nums_4, .numbers_count = 5
};
static const benchmark_Level4 *g_dn_items_level4[] = {
	&g_dn_items_level4_0, &g_dn_items_level4_1, &g_dn_items_level4_2,
	&g_dn_items_level4_3, &g_dn_items_level4_4,
};

/* level3 items sub-lists for nested level3.items */
static int32_t g_dn_sub_nums_0[] = { 1, 2 };
static int32_t g_dn_sub_nums_1[] = { 3, 4 };
static int32_t g_dn_sub_nums_2[] = { 5, 6 };
static int32_t g_dn_sub_nums_3[] = { 7, 8 };

static benchmark_Level4 g_dn_sub_items_1_0 = {
	.value = 311, .data = { .data = (const uint8_t *)"sub1", .len = 4 },
	.numbers = g_dn_sub_nums_0, .numbers_count = 2
};
static benchmark_Level4 g_dn_sub_items_1_1 = {
	.value = 312, .data = { .data = (const uint8_t *)"sub2", .len = 4 },
	.numbers = g_dn_sub_nums_1, .numbers_count = 2
};
static const benchmark_Level4 *g_dn_sub_items_1[] = {
	&g_dn_sub_items_1_0, &g_dn_sub_items_1_1,
};

static benchmark_Level4 g_dn_sub_items_2_0 = {
	.value = 321, .data = { .data = (const uint8_t *)"sub3", .len = 4 },
	.numbers = g_dn_sub_nums_2, .numbers_count = 2
};
static benchmark_Level4 g_dn_sub_items_2_1 = {
	.value = 322, .data = { .data = (const uint8_t *)"sub4", .len = 4 },
	.numbers = g_dn_sub_nums_3, .numbers_count = 2
};
static const benchmark_Level4 *g_dn_sub_items_2[] = {
	&g_dn_sub_items_2_0, &g_dn_sub_items_2_1,
};

/* level3 items (the outer list, under Level2.items[0].Level3[_].items) */
static int32_t g_dn_l3_nested_nums_0[] = { 1, 2, 3, 4, 5 };
static int32_t g_dn_l3_nested_nums_1[] = { 6, 7, 8, 9, 10 };
static int32_t g_dn_l3_nested_nums_2[] = { 11, 12, 13, 14, 15 };
static int32_t g_dn_l3_nested_nums_3[] = { 16, 17, 18, 19, 20 };

static benchmark_Level4 g_dn_l3_nested_0 = {
	.value = 310, .data = { .data = (const uint8_t *)"nested_item1_data", .len = 17 },
	.numbers = g_dn_l3_nested_nums_0, .numbers_count = 5
};
static benchmark_Level4 g_dn_l3_nested_1 = {
	.value = 320, .data = { .data = (const uint8_t *)"nested_item2_data", .len = 17 },
	.numbers = g_dn_l3_nested_nums_1, .numbers_count = 5
};
static benchmark_Level4 g_dn_l3_nested_2 = {
	.value = 330, .data = { .data = (const uint8_t *)"nested_item3_data", .len = 17 },
	.numbers = g_dn_l3_nested_nums_2, .numbers_count = 5
};
static benchmark_Level4 g_dn_l3_nested_3 = {
	.value = 340, .data = { .data = (const uint8_t *)"nested_item4_data", .len = 17 },
	.numbers = g_dn_l3_nested_nums_3, .numbers_count = 5
};

static benchmark_Level3 g_dn_items_level3_0 = {
	.id = 31, .name = { .data = (const uint8_t *)"item1_nested", .len = 12 },
	.nested = &g_dn_l3_nested_0,
	.items = g_dn_sub_items_1, .items_count = 2,
};
static benchmark_Level3 g_dn_items_level3_1 = {
	.id = 32, .name = { .data = (const uint8_t *)"item2_nested", .len = 12 },
	.nested = &g_dn_l3_nested_1,
	.items = g_dn_sub_items_2, .items_count = 2,
};
static benchmark_Level3 g_dn_items_level3_2 = {
	.id = 33, .name = { .data = (const uint8_t *)"item3_nested", .len = 12 },
	.nested = &g_dn_l3_nested_2,
};
static benchmark_Level3 g_dn_items_level3_3 = {
	.id = 34, .name = { .data = (const uint8_t *)"item4_nested", .len = 12 },
	.nested = &g_dn_l3_nested_3,
};
static const benchmark_Level3 *g_dn_items_level3[] = {
	&g_dn_items_level3_0, &g_dn_items_level3_1,
	&g_dn_items_level3_2, &g_dn_items_level3_3,
};

/* Deeply-nested chain: Level4 (leaf, under primary branch) */
static int32_t g_dn_primary_l4_nums[] = { 100, 200, 300 };
static benchmark_Level4 g_dn_primary_l4 = {
	.value = 2100,
	.data = { .data = (const uint8_t *)"deep_nested", .len = 11 },
	.numbers = g_dn_primary_l4_nums, .numbers_count = 3,
};

/* Level2 items under DeepNested.nested.items (Level1.items) */
static benchmark_Level3 g_dn_lvl2_items_0_nested = {
	.id = 210,
	.name = { .data = (const uint8_t *)"nested_in_item1", .len = 15 },
	.nested = &g_dn_primary_l4,
};

/* outer Level2.items[0].nested (Level3) — same shape as createDeepNested. */
static benchmark_Level3 g_dn_outer_lvl2_items_0_nested = {
	.id = 220,
	.name = { .data = (const uint8_t *)"nested_in_item2", .len = 15 },
};

static benchmark_Level2 g_dn_level2_items_0 = {
	.id = 21,
	.description = { .data = (const uint8_t *)"item1_level2_with_payload", .len = 25 },
	.payload = { .data = (const uint8_t *)"payload for item 1", .len = 18 },
	.nested = &g_dn_lvl2_items_0_nested,
};
static benchmark_Level2 g_dn_level2_items_1 = {
	.id = 22,
	.description = { .data = (const uint8_t *)"item2_level2_with_payload", .len = 25 },
	.payload = { .data = (const uint8_t *)"payload for item 2 with more data", .len = 33 },
	.nested = &g_dn_outer_lvl2_items_0_nested,
};
static benchmark_Level2 g_dn_level2_items_2 = {
	.id = 23,
	.description = { .data = (const uint8_t *)"item3_level2_with_payload", .len = 25 },
	.payload = { .data = (const uint8_t *)"payload for item 3", .len = 18 },
};
static benchmark_Level2 g_dn_level2_items_3 = {
	.id = 24,
	.description = { .data = (const uint8_t *)"item4_level2_with_payload", .len = 25 },
	.payload = { .data = (const uint8_t *)"payload for item 4 with additional content", .len = 43 },
};
static const benchmark_Level2 *g_dn_items_level2[] = {
	&g_dn_level2_items_0, &g_dn_level2_items_1,
	&g_dn_level2_items_2, &g_dn_level2_items_3,
};

/* Primary branch: DeepNested.nested = Level1 → Level2 → Level3 → Level4. */
static benchmark_Level4 g_dn_primary_l4_leaf = {
	.value = 40,
	.data = { .data = (const uint8_t *)"level4_leaf_node_with_data", .len = 26 },
	.numbers = g_dn_lvl4_numbers_main, .numbers_count = 15,
};

static benchmark_Level3 g_dn_primary_l3_outer = {
	.id = 30,
	.name = { .data = (const uint8_t *)"level3_inner_structure", .len = 22 },
	.nested = &g_dn_primary_l4_leaf,
	.items = g_dn_items_level4, .items_count = 5,
};

static benchmark_Level2 g_dn_primary_l2 = {
	.id = 20,
	.description = { .data = (const uint8_t *)"level2_deeply_nested_with_payload", .len = 33 },
	.payload = {
		.data = (const uint8_t *)
			"this is a much longer payload with more realistic data content "
			"that would be found in production systems",
		.len = 105
	},
	.nested = &g_dn_primary_l3_outer,
	.items = g_dn_items_level3, .items_count = 4,
};

static benchmark_Level1 g_dn_primary_l1 = {
	.id = 10,
	.title = { .data = (const uint8_t *)"level1_primary_branch", .len = 21 },
	.score = 1.23456789,
	.nested = &g_dn_primary_l2,
	.items = g_dn_items_level2, .items_count = 4,
};

/* DeepNested.items — list of Level1 siblings. */
static benchmark_Level3 g_dn_item_l3_1 = {
	.id = 1100,
	.name = { .data = (const uint8_t *)"deeply_nested_top_item1", .len = 23 },
};
static benchmark_Level2 g_dn_item_l2_1 = {
	.id = 110,
	.description = { .data = (const uint8_t *)"nested_in_top_item1", .len = 19 },
	.payload = { .data = (const uint8_t *)"some payload data", .len = 17 },
	.nested = &g_dn_item_l3_1,
};
static benchmark_Level1 g_dn_item_l1_0 = {
	.id = 11,
	.title = { .data = (const uint8_t *)"item1_top_level", .len = 15 },
	.score = 2.34,
	.nested = &g_dn_item_l2_1,
};

static benchmark_Level2 g_dn_item_l2_2 = {
	.id = 120,
	.description = { .data = (const uint8_t *)"nested_in_top_item2", .len = 19 },
	.payload = { .data = (const uint8_t *)"more payload data here", .len = 22 },
};
static benchmark_Level1 g_dn_item_l1_1 = {
	.id = 12,
	.title = { .data = (const uint8_t *)"item2_top_level", .len = 15 },
	.score = 3.45,
	.nested = &g_dn_item_l2_2,
};

static benchmark_Level1 g_dn_item_l1_2 = {
	.id = 13,
	.title = { .data = (const uint8_t *)"item3_top_level", .len = 15 },
	.score = 4.56,
};

static benchmark_Level2 g_dn_item_l2_4 = {
	.id = 140,
	.description = { .data = (const uint8_t *)"nested_in_top_item4", .len = 19 },
	.payload = { .data = (const uint8_t *)"final payload data", .len = 18 },
};
static benchmark_Level1 g_dn_item_l1_3 = {
	.id = 14,
	.title = { .data = (const uint8_t *)"item4_top_level", .len = 15 },
	.score = 5.67,
	.nested = &g_dn_item_l2_4,
};

static benchmark_Level1 g_dn_item_l1_4 = {
	.id = 15,
	.title = { .data = (const uint8_t *)"item5_top_level", .len = 15 },
	.score = 6.78,
};

static const benchmark_Level1 *g_dn_items_level1[] = {
	&g_dn_item_l1_0, &g_dn_item_l1_1, &g_dn_item_l1_2,
	&g_dn_item_l1_3, &g_dn_item_l1_4,
};

static struct gremlin_bytes g_dn_tags[] = {
	{ .data = (const uint8_t *)"tag1", .len = 4 },
	{ .data = (const uint8_t *)"tag2", .len = 4 },
	{ .data = (const uint8_t *)"tag3", .len = 4 },
	{ .data = (const uint8_t *)"tag4", .len = 4 },
	{ .data = (const uint8_t *)"tag5", .len = 4 },
	{ .data = (const uint8_t *)"tag6", .len = 4 },
	{ .data = (const uint8_t *)"tag7", .len = 4 },
	{ .data = (const uint8_t *)"tag8", .len = 4 },
};

benchmark_DeepNested
create_deep_nested(void)
{
	benchmark_DeepNested m = {
		.root_id = 1,
		.root_name = {
			.data = (const uint8_t *)"root_node_with_complex_nested_structure",
			.len = 39
		},
		.active = true,
		.tags = g_dn_tags, .tags_count = 8,
		.nested = &g_dn_primary_l1,
		.items = g_dn_items_level1, .items_count = 5,
	};
	return m;
}

/* ========================================================================
 * Benchmark runner primitives
 * ======================================================================== */

static int64_t
now_ns(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (int64_t)t.tv_sec * 1000000000LL + (int64_t)t.tv_nsec;
}

/* Format `n` with underscores every three digits: 10000000 → 10_000_000. */
static void
format_with_underscores(size_t n, char *buf, size_t cap)
{
	char tmp[32];
	int tl = snprintf(tmp, sizeof tmp, "%zu", n);
	if (tl <= 0) { buf[0] = '\0'; return; }
	size_t o = 0;
	for (int i = 0; i < tl && o + 1 < cap; i++) {
		if (i > 0 && ((tl - i) % 3) == 0) {
			if (o + 1 >= cap) break;
			buf[o++] = '_';
		}
		buf[o++] = tmp[i];
	}
	buf[o] = '\0';
}

static void
print_cpu_info(void)
{
	/* Linux: parse /proc/cpuinfo for "model name". */
	FILE *f = fopen("/proc/cpuinfo", "r");
	const char *label = "Unknown";
	char line[512];
	char name[256] = {0};
	if (f) {
		while (fgets(line, sizeof line, f)) {
			if (strncmp(line, "model name", 10) == 0) {
				char *colon = strchr(line, ':');
				if (colon) {
					colon++;
					while (*colon == ' ' || *colon == '\t') colon++;
					size_t l = strlen(colon);
					while (l > 0 && (colon[l-1] == '\n' || colon[l-1] == ' ')) l--;
					if (l >= sizeof name) l = sizeof name - 1;
					memcpy(name, colon, l);
					name[l] = '\0';
					label = name;
					break;
				}
			}
		}
		fclose(f);
	}
	fprintf(stderr, "CPU: %s\n", label);

	long cores = sysconf(_SC_NPROCESSORS_ONLN);
	fprintf(stderr, "CPU Cores: %ld\n", cores > 0 ? cores : 1);
}

/* ========================================================================
 * DeepNested benchmarks
 * ======================================================================== */

static int64_t
bench_marshal_deep(const benchmark_DeepNested *msg, uint8_t *data, size_t cap,
		   size_t iterations)
{
	for (size_t i = 0; i < 100; i++) {
		struct gremlin_writer w;
		gremlin_writer_init(&w, data, cap);
		benchmark_DeepNested_encode(msg, &w);
	}
	int64_t start = now_ns();
	for (size_t i = 0; i < iterations; i++) {
		struct gremlin_writer w;
		gremlin_writer_init(&w, data, cap);
		benchmark_DeepNested_encode(msg, &w);
	}
	int64_t end = now_ns();
	return (end - start) / (int64_t)iterations;
}

static int64_t
bench_unmarshal_deep(const uint8_t *data, size_t len, size_t iterations)
{
	benchmark_DeepNested_reader r;
	for (size_t i = 0; i < 100; i++) {
		(void)benchmark_DeepNested_reader_init(&r, data, len);
	}
	int64_t start = now_ns();
	for (size_t i = 0; i < iterations; i++) {
		(void)benchmark_DeepNested_reader_init(&r, data, len);
	}
	int64_t end = now_ns();
	return (end - start) / (int64_t)iterations;
}

static int64_t
bench_lazy_deep(const uint8_t *data, size_t len, size_t iterations)
{
	benchmark_DeepNested_reader r;
	volatile int32_t root_id_sink = 0;
	volatile bool    active_sink  = false;
	volatile size_t  name_sink    = 0;
	for (size_t i = 0; i < 100; i++) {
		(void)benchmark_DeepNested_reader_init(&r, data, len);
		root_id_sink = benchmark_DeepNested_reader_get_root_id(&r);
		name_sink    = benchmark_DeepNested_reader_get_root_name(&r).len;
		active_sink  = benchmark_DeepNested_reader_get_active(&r);
	}
	int64_t start = now_ns();
	for (size_t i = 0; i < iterations; i++) {
		(void)benchmark_DeepNested_reader_init(&r, data, len);
		root_id_sink = benchmark_DeepNested_reader_get_root_id(&r);
		name_sink    = benchmark_DeepNested_reader_get_root_name(&r).len;
		active_sink  = benchmark_DeepNested_reader_get_active(&r);
	}
	int64_t end = now_ns();
	(void)root_id_sink; (void)active_sink; (void)name_sink;
	return (end - start) / (int64_t)iterations;
}

static int64_t
bench_deep_access_deep(const uint8_t *data, size_t len, size_t iterations)
{
	benchmark_DeepNested_reader r;
	benchmark_Level1_reader n1;
	benchmark_Level2_reader n2;
	benchmark_Level3_reader n3;
	benchmark_Level4_reader n4;
	volatile int32_t sink = 0;
	for (size_t i = 0; i < 100; i++) {
		(void)benchmark_DeepNested_reader_init(&r, data, len);
		sink = benchmark_DeepNested_reader_get_root_id(&r);
		(void)benchmark_DeepNested_reader_get_nested(&r, &n1);
		sink = benchmark_Level1_reader_get_id(&n1);
		(void)benchmark_Level1_reader_get_nested(&n1, &n2);
		sink = benchmark_Level2_reader_get_id(&n2);
		(void)benchmark_Level2_reader_get_nested(&n2, &n3);
		sink = benchmark_Level3_reader_get_id(&n3);
		(void)benchmark_Level3_reader_get_nested(&n3, &n4);
		sink = benchmark_Level4_reader_get_value(&n4);
	}
	int64_t start = now_ns();
	for (size_t i = 0; i < iterations; i++) {
		(void)benchmark_DeepNested_reader_init(&r, data, len);
		sink = benchmark_DeepNested_reader_get_root_id(&r);
		(void)benchmark_DeepNested_reader_get_nested(&r, &n1);
		sink = benchmark_Level1_reader_get_id(&n1);
		(void)benchmark_Level1_reader_get_nested(&n1, &n2);
		sink = benchmark_Level2_reader_get_id(&n2);
		(void)benchmark_Level2_reader_get_nested(&n2, &n3);
		sink = benchmark_Level3_reader_get_id(&n3);
		(void)benchmark_Level3_reader_get_nested(&n3, &n4);
		sink = benchmark_Level4_reader_get_value(&n4);
	}
	int64_t end = now_ns();
	(void)sink;
	return (end - start) / (int64_t)iterations;
}

/* ========================================================================
 * Golden TestAllTypes benchmarks
 * ======================================================================== */

static int64_t
bench_marshal_golden(protobuf_unittest_TestAllTypes *msg,
		     uint8_t *data, size_t cap, size_t iterations)
{
	for (int32_t i = 0; i < 100; i++) {
		update_golden_message(msg, i);
		struct gremlin_writer w;
		gremlin_writer_init(&w, data, cap);
		protobuf_unittest_TestAllTypes_encode(msg, &w);
	}
	int64_t start = now_ns();
	for (size_t i = 0; i < iterations; i++) {
		update_golden_message(msg, (int32_t)i);
		struct gremlin_writer w;
		gremlin_writer_init(&w, data, cap);
		protobuf_unittest_TestAllTypes_encode(msg, &w);
	}
	int64_t end = now_ns();
	return (end - start) / (int64_t)iterations;
}

static int64_t
bench_unmarshal_golden(const uint8_t *data, size_t len, size_t iterations)
{
	protobuf_unittest_TestAllTypes_reader r;
	for (size_t i = 0; i < 100; i++) {
		(void)protobuf_unittest_TestAllTypes_reader_init(&r, data, len);
	}
	int64_t start = now_ns();
	for (size_t i = 0; i < iterations; i++) {
		(void)protobuf_unittest_TestAllTypes_reader_init(&r, data, len);
	}
	int64_t end = now_ns();
	return (end - start) / (int64_t)iterations;
}

static int64_t
bench_lazy_golden(const uint8_t *data, size_t len, size_t iterations)
{
	protobuf_unittest_TestAllTypes_reader r;
	volatile int32_t i32_sink = 0;
	volatile int64_t i64_sink = 0;
	volatile size_t  str_sink = 0;
	for (size_t i = 0; i < 100; i++) {
		(void)protobuf_unittest_TestAllTypes_reader_init(&r, data, len);
		i32_sink = protobuf_unittest_TestAllTypes_reader_get_optional_int32(&r);
		i64_sink = protobuf_unittest_TestAllTypes_reader_get_optional_int64(&r);
		str_sink = protobuf_unittest_TestAllTypes_reader_get_optional_string(&r).len;
	}
	int64_t start = now_ns();
	for (size_t i = 0; i < iterations; i++) {
		(void)protobuf_unittest_TestAllTypes_reader_init(&r, data, len);
		i32_sink = protobuf_unittest_TestAllTypes_reader_get_optional_int32(&r);
		i64_sink = protobuf_unittest_TestAllTypes_reader_get_optional_int64(&r);
		str_sink = protobuf_unittest_TestAllTypes_reader_get_optional_string(&r).len;
	}
	int64_t end = now_ns();
	(void)i32_sink; (void)i64_sink; (void)str_sink;
	return (end - start) / (int64_t)iterations;
}

static int64_t
bench_deep_access_golden(const uint8_t *data, size_t len, size_t iterations)
{
	protobuf_unittest_TestAllTypes_reader r;
	protobuf_unittest_TestAllTypes_NestedMessage_reader nested;
	protobuf_unittest_ForeignMessage_reader             foreign;
	protobuf_unittest_import_ImportMessage_reader       import_r;
	volatile int32_t sink = 0;
	volatile size_t  str_sink = 0;
	for (size_t i = 0; i < 100; i++) {
		(void)protobuf_unittest_TestAllTypes_reader_init(&r, data, len);
		sink = protobuf_unittest_TestAllTypes_reader_get_optional_int32(&r);
		str_sink = protobuf_unittest_TestAllTypes_reader_get_optional_string(&r).len;
		(void)protobuf_unittest_TestAllTypes_reader_get_optional_nested_message(&r, &nested);
		sink = protobuf_unittest_TestAllTypes_NestedMessage_reader_get_bb(&nested);
		(void)protobuf_unittest_TestAllTypes_reader_get_optional_foreign_message(&r, &foreign);
		sink = protobuf_unittest_ForeignMessage_reader_get_c(&foreign);
		(void)protobuf_unittest_TestAllTypes_reader_get_optional_import_message(&r, &import_r);
		sink = protobuf_unittest_import_ImportMessage_reader_get_d(&import_r);
	}
	int64_t start = now_ns();
	for (size_t i = 0; i < iterations; i++) {
		(void)protobuf_unittest_TestAllTypes_reader_init(&r, data, len);
		sink = protobuf_unittest_TestAllTypes_reader_get_optional_int32(&r);
		str_sink = protobuf_unittest_TestAllTypes_reader_get_optional_string(&r).len;
		(void)protobuf_unittest_TestAllTypes_reader_get_optional_nested_message(&r, &nested);
		sink = protobuf_unittest_TestAllTypes_NestedMessage_reader_get_bb(&nested);
		(void)protobuf_unittest_TestAllTypes_reader_get_optional_foreign_message(&r, &foreign);
		sink = protobuf_unittest_ForeignMessage_reader_get_c(&foreign);
		(void)protobuf_unittest_TestAllTypes_reader_get_optional_import_message(&r, &import_r);
		sink = protobuf_unittest_import_ImportMessage_reader_get_d(&import_r);
	}
	int64_t end = now_ns();
	(void)sink; (void)str_sink;
	return (end - start) / (int64_t)iterations;
}

/* ========================================================================
 * Main
 * ======================================================================== */

int
main(int argc, char **argv)
{
	size_t iterations = 10000000;
	if (argc >= 2) {
		char *end;
		unsigned long long v = strtoull(argv[1], &end, 10);
		if (*end == '\0' && v > 0) iterations = (size_t)v;
	}

	fprintf(stderr, "===========================================\n");
	fprintf(stderr, "gremlin.c Benchmarks\n");
	fprintf(stderr, "===========================================\n");
	print_cpu_info();

	char ibuf[32];
	format_with_underscores(iterations, ibuf, sizeof ibuf);
	fprintf(stderr, "Iterations: %s\n", ibuf);
	fprintf(stderr, "===========================================\n\n");

	/* ---- Deep Nested ---- */
	fprintf(stderr, "DEEP NESTED MESSAGE BENCHMARKS\n");
	fprintf(stderr, "-------------------------------------------\n");
	benchmark_DeepNested deep = create_deep_nested();
	size_t deep_size = benchmark_DeepNested_size(&deep);
	uint8_t *deep_buf = malloc(deep_size);
	if (!deep_buf) { fprintf(stderr, "oom\n"); return 1; }
	struct gremlin_writer dw;
	gremlin_writer_init(&dw, deep_buf, deep_size);
	benchmark_DeepNested_encode(&deep, &dw);

	fprintf(stderr, "Message Size: %zu bytes\n\n", deep_size);

	fprintf(stderr, "Marshal (Serialize):\n");
	int64_t t = bench_marshal_deep(&deep, deep_buf, deep_size, iterations);
	fprintf(stderr, "  %ld ns/op\n\n", (long)t);

	fprintf(stderr, "Unmarshal (Deserialize):\n");
	t = bench_unmarshal_deep(deep_buf, deep_size, iterations);
	fprintf(stderr, "  %ld ns/op\n\n", (long)t);

	fprintf(stderr, "Unmarshal + Root Access (Lazy Parsing):\n");
	t = bench_lazy_deep(deep_buf, deep_size, iterations);
	fprintf(stderr, "  %ld ns/op\n\n", (long)t);

	fprintf(stderr, "Full Deep Access (All Nested Fields):\n");
	t = bench_deep_access_deep(deep_buf, deep_size, iterations);
	fprintf(stderr, "  %ld ns/op\n", (long)t);

	free(deep_buf);
	fprintf(stderr, "\n");

	/* ---- Golden ---- */
	fprintf(stderr, "GOLDEN MESSAGE BENCHMARKS (protobuf_unittest)\n");
	fprintf(stderr, "-------------------------------------------\n");

	size_t golden_len;
	uint8_t *golden = test_load_binary("golden_message", &golden_len);
	fprintf(stderr, "Message Size: %zu bytes\n\n", golden_len);

	protobuf_unittest_TestAllTypes msg = create_golden_message();
	size_t golden_cap = protobuf_unittest_TestAllTypes_size(&msg);
	uint8_t *golden_buf = malloc(golden_cap);
	if (!golden_buf) { fprintf(stderr, "oom\n"); return 1; }

	fprintf(stderr, "Marshal (Serialize):\n");
	t = bench_marshal_golden(&msg, golden_buf, golden_cap, iterations);
	fprintf(stderr, "  %ld ns/op\n\n", (long)t);

	fprintf(stderr, "Unmarshal (Deserialize):\n");
	t = bench_unmarshal_golden(golden, golden_len, iterations);
	fprintf(stderr, "  %ld ns/op\n\n", (long)t);

	fprintf(stderr, "Unmarshal + Root Access (Lazy Parsing):\n");
	t = bench_lazy_golden(golden, golden_len, iterations);
	fprintf(stderr, "  %ld ns/op\n\n", (long)t);

	fprintf(stderr, "Deep Access (Including Nested Messages):\n");
	t = bench_deep_access_golden(golden, golden_len, iterations);
	fprintf(stderr, "  %ld ns/op\n", (long)t);

	free(golden_buf);
	free(golden);

	fprintf(stderr, "\n===========================================\n");
	fprintf(stderr, "Benchmark Complete!\n");
	fprintf(stderr, "===========================================\n");
	return 0;
}

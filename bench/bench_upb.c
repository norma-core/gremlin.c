#include <stddef.h>
#include <stdint.h>

#include "upb/mem/arena.h"
#include "upb/message/message.h"

#include "bench.h"
#include "benchmark.upb.h"
#include "unittest.upb.h"
#include "unittest_import.upb.h"

static _Alignas(16) char arena_block[64 * 1024];

/* An arena per call over a reused first block: no heap traffic in the loop. */
#define WITH_ARENA(a, ...)                                                       \
	do {                                                                     \
		upb_Arena *a = upb_Arena_Init(arena_block, sizeof arena_block,   \
		    &upb_alloc_global);                                          \
		__VA_ARGS__;                                                     \
		upb_Arena_Free(a);                                               \
	} while (0)

static void
bench_deep(const char *data, size_t len, size_t n)
{
	volatile int64_t sink = 0;
	upb_Arena *keep = upb_Arena_New();
	benchmark_DeepNested *msg = benchmark_DeepNested_parse(data, len, keep);
	if (!msg) {
		fprintf(stderr, "upb: cannot parse deep_nested\n");
		exit(1);
	}

	BENCH("deep", "marshal", n, WITH_ARENA(a, {
		size_t out;
		sink = (int64_t)(uintptr_t)benchmark_DeepNested_serialize(msg, a, &out);
	}));

	BENCH("deep", "unmarshal", n, WITH_ARENA(a, {
		sink = (int64_t)(uintptr_t)benchmark_DeepNested_parse(data, len, a);
	}));

	BENCH("deep", "lazy", n, WITH_ARENA(a, {
		const benchmark_DeepNested *m = benchmark_DeepNested_parse(data, len, a);
		sink = benchmark_DeepNested_root_id(m);
		sink = (int64_t)benchmark_DeepNested_root_name(m).size;
		sink = benchmark_DeepNested_active(m);
	}));

	BENCH("deep", "deep", n, WITH_ARENA(a, {
		const benchmark_DeepNested *m = benchmark_DeepNested_parse(data, len, a);
		const benchmark_Level1 *l1 = benchmark_DeepNested_nested(m);
		const benchmark_Level2 *l2 = benchmark_Level1_nested(l1);
		const benchmark_Level3 *l3 = benchmark_Level2_nested(l2);
		sink = benchmark_DeepNested_root_id(m);
		sink = benchmark_Level1_id(l1);
		sink = benchmark_Level2_id(l2);
		sink = benchmark_Level3_id(l3);
		sink = benchmark_Level4_value(benchmark_Level3_nested(l3));
	}));
	(void)sink;
	upb_Arena_Free(keep);
}

static void
discard_unknown(upb_Message *m, upb_Arena *a)
{
	/* A bound, so a delete that keeps its region fails the check below, not hangs. */
	for (int pass = 0; pass < 1000; pass++) {
		uintptr_t iter = kUpb_Message_UnknownBegin;
		upb_StringView d;
		if (!upb_Message_NextUnknown(m, &d, &iter) ||
		    upb_Message_DeleteUnknown(m, &d, &iter, a) == kUpb_DeleteUnknown_AllocFail)
			break;
	}
	if (upb_Message_HasUnknown(m)) {
		fprintf(stderr, "upb: cannot drop unknown fields\n");
		exit(1);
	}
}

static void
bench_golden(const char *data, size_t len, size_t n)
{
	volatile int64_t sink = 0;
	upb_Arena *keep = upb_Arena_New();
	protobuf_unittest_TestAllTypes *msg = protobuf_unittest_TestAllTypes_parse(data, len, keep);
	if (!msg) {
		fprintf(stderr, "upb: cannot parse golden_message\n");
		exit(1);
	}
	discard_unknown(UPB_UPCAST(msg), keep);
	/* C++ drops unknown fields at every depth; the sub-messages marshal writes have none. */
	const upb_Message *subs[] = {
		UPB_UPCAST(protobuf_unittest_TestAllTypes_optional_nested_message(msg)),
		UPB_UPCAST(protobuf_unittest_TestAllTypes_optional_foreign_message(msg)),
		UPB_UPCAST(protobuf_unittest_TestAllTypes_optional_import_message(msg)),
	};
	for (size_t i = 0; i < sizeof subs / sizeof subs[0]; i++) {
		if (upb_Message_HasUnknown(subs[i])) {
			fprintf(stderr, "upb: unknown fields in a sub-message\n");
			exit(1);
		}
	}
	size_t nrep;
	protobuf_unittest_TestAllTypes_repeated_nested_message(msg, &nrep);
	if (nrep < 2) {
		fprintf(stderr, "upb: golden_message has under two repeated_nested_message\n");
		exit(1);
	}

	BENCH("golden", "marshal", n, WITH_ARENA(a, {
		int32_t i = (int32_t)(bench_i & 1023);
		size_t rn, out;
		protobuf_unittest_TestAllTypes_NestedMessage **rep =
		    protobuf_unittest_TestAllTypes_mutable_repeated_nested_message(msg, &rn);
		protobuf_unittest_TestAllTypes_set_optional_int32(msg, 101 + i);
		protobuf_unittest_TestAllTypes_NestedMessage_set_bb(
		    protobuf_unittest_TestAllTypes_mutable_optional_nested_message(msg, keep), 118 + i);
		protobuf_unittest_TestAllTypes_NestedMessage_set_bb(rep[0], 218 + i);
		protobuf_unittest_TestAllTypes_NestedMessage_set_bb(rep[1], 318 + i);
		protobuf_unittest_TestAllTypes_set_oneof_uint32(msg, (uint32_t)(601 + i));
		sink = (int64_t)(uintptr_t)protobuf_unittest_TestAllTypes_serialize(msg, a, &out);
	}));

	BENCH("golden", "unmarshal", n, WITH_ARENA(a, {
		sink = (int64_t)(uintptr_t)protobuf_unittest_TestAllTypes_parse(data, len, a);
	}));

	BENCH("golden", "lazy", n, WITH_ARENA(a, {
		const protobuf_unittest_TestAllTypes *m = protobuf_unittest_TestAllTypes_parse(data, len, a);
		sink = protobuf_unittest_TestAllTypes_optional_int32(m);
		sink = protobuf_unittest_TestAllTypes_optional_int64(m);
		sink = (int64_t)protobuf_unittest_TestAllTypes_optional_string(m).size;
	}));

	BENCH("golden", "deep", n, WITH_ARENA(a, {
		const protobuf_unittest_TestAllTypes *m = protobuf_unittest_TestAllTypes_parse(data, len, a);
		sink = protobuf_unittest_TestAllTypes_optional_int32(m);
		sink = (int64_t)protobuf_unittest_TestAllTypes_optional_string(m).size;
		sink = protobuf_unittest_TestAllTypes_NestedMessage_bb(
		    protobuf_unittest_TestAllTypes_optional_nested_message(m));
		sink = protobuf_unittest_ForeignMessage_c(
		    protobuf_unittest_TestAllTypes_optional_foreign_message(m));
		sink = protobuf_unittest_import_ImportMessage_d(
		    protobuf_unittest_TestAllTypes_optional_import_message(m));
	}));
	(void)sink;
	upb_Arena_Free(keep);
}

int
main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s iterations deep_nested.bin golden_message\n", argv[0]);
		return 2;
	}
	size_t n = bench_iterations(argc, argv);
	size_t deep_len, golden_len;
	uint8_t *deep = bench_load(argv[2], &deep_len);
	uint8_t *golden = bench_load(argv[3], &golden_len);
	bench_deep((const char *)deep, deep_len, n);
	bench_golden((const char *)golden, golden_len, n);
	free(deep);
	free(golden);
	return 0;
}

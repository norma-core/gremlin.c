#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bench.h"
#include "benchmark.pb-c.h"
#include "unittest.pb-c.h"
#include "unittest_import.pb-c.h"

/* A bump allocator reset before every unpack: no heap traffic in the loop. */
static _Alignas(16) uint8_t pool[64 * 1024];
static size_t pool_used;

static void *
pool_alloc(void *data, size_t size)
{
	(void)data;
	size = (size + 15u) & ~(size_t)15u;
	if (pool_used + size > sizeof pool) {
		fprintf(stderr, "protobuf-c: pool exhausted\n");
		exit(1);
	}
	void *p = pool + pool_used;
	pool_used += size;
	return p;
}

static void
pool_free(void *data, void *p)
{
	(void)data;
	(void)p;
}

static ProtobufCAllocator pool_allocator = { pool_alloc, pool_free, NULL };

static void
bench_deep(const uint8_t *data, size_t len, size_t n)
{
	volatile int64_t sink = 0;
	uint8_t out[4096];
	Benchmark__DeepNested *msg = benchmark__deep_nested__unpack(NULL, len, data);
	if (!msg) {
		fprintf(stderr, "protobuf-c: cannot parse deep_nested\n");
		exit(1);
	}

	if (benchmark__deep_nested__get_packed_size(msg) > sizeof out) {
		fprintf(stderr, "protobuf-c: deep_nested does not fit the buffer\n");
		exit(1);
	}
	BENCH("deep", "marshal", n, sink = (int64_t)benchmark__deep_nested__pack(msg, out));

	BENCH("deep", "unmarshal", n, {
		pool_used = 0;
		sink = (int64_t)(uintptr_t)benchmark__deep_nested__unpack(&pool_allocator, len, data);
	});

	BENCH("deep", "lazy", n, {
		pool_used = 0;
		Benchmark__DeepNested *m = benchmark__deep_nested__unpack(&pool_allocator, len, data);
		sink = m->root_id;
		sink = (int64_t)strlen(m->root_name);
		sink = m->active;
	});

	BENCH("deep", "deep", n, {
		pool_used = 0;
		Benchmark__DeepNested *m = benchmark__deep_nested__unpack(&pool_allocator, len, data);
		sink = m->root_id;
		sink = m->nested->id;
		sink = m->nested->nested->id;
		sink = m->nested->nested->nested->id;
		sink = m->nested->nested->nested->nested->value;
	});
	(void)sink;
	benchmark__deep_nested__free_unpacked(msg, NULL);
}

static void
bench_golden(const uint8_t *data, size_t len, size_t n)
{
	volatile int64_t sink = 0;
	uint8_t out[4096];
	ProtobufUnittest__TestAllTypes *msg = protobuf_unittest__test_all_types__unpack(NULL, len, data);
	/* protobuf-c rejects the group wire types, and golden_message has groups. */
	if (!msg) {
		fprintf(stderr, "protobuf-c: cannot parse golden_message, skipped\n");
		return;
	}
	size_t unknown = msg->base.n_unknown_fields;
	msg->base.n_unknown_fields = 0;
	/* Room for the updated values, which take a few more bytes than the file's. */
	if (msg->n_repeated_nested_message < 2 ||
	    protobuf_unittest__test_all_types__get_packed_size(msg) + 64 > sizeof out) {
		fprintf(stderr, "protobuf-c: golden_message does not fit the benchmark\n");
		exit(1);
	}

	BENCH("golden", "marshal", n, {
		int32_t i = (int32_t)(bench_i & 1023);
		msg->optional_int32 = 101 + i;
		msg->optional_nested_message->bb = 118 + i;
		msg->repeated_nested_message[0]->bb = 218 + i;
		msg->repeated_nested_message[1]->bb = 318 + i;
		msg->oneof_field_case = PROTOBUF_UNITTEST__TEST_ALL_TYPES__ONEOF_FIELD_ONEOF_UINT32;
		msg->oneof_uint32 = (uint32_t)(601 + i);
		sink = (int64_t)protobuf_unittest__test_all_types__pack(msg, out);
	});

	BENCH("golden", "unmarshal", n, {
		pool_used = 0;
		sink = (int64_t)(uintptr_t)protobuf_unittest__test_all_types__unpack(&pool_allocator, len, data);
	});

	BENCH("golden", "lazy", n, {
		pool_used = 0;
		ProtobufUnittest__TestAllTypes *m =
		    protobuf_unittest__test_all_types__unpack(&pool_allocator, len, data);
		sink = m->optional_int32;
		sink = m->optional_int64;
		sink = (int64_t)strlen(m->optional_string);
	});

	BENCH("golden", "deep", n, {
		pool_used = 0;
		ProtobufUnittest__TestAllTypes *m =
		    protobuf_unittest__test_all_types__unpack(&pool_allocator, len, data);
		sink = m->optional_int32;
		sink = (int64_t)strlen(m->optional_string);
		sink = m->optional_nested_message->bb;
		sink = m->optional_foreign_message->c;
		sink = m->optional_import_message->d;
	});
	(void)sink;
	msg->base.n_unknown_fields = unknown;
	protobuf_unittest__test_all_types__free_unpacked(msg, NULL);
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
	bench_deep(deep, deep_len, n);
	bench_golden(golden, golden_len, n);
	free(deep);
	free(golden);
	return 0;
}

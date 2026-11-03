#include <cstddef>
#include <cstdint>

#include <google/protobuf/arena.h>
#include <google/protobuf/stubs/common.h>

#include "bench.h"
#include "benchmark.pb.h"
#include "unittest.pb.h"

using google::protobuf::Arena;
using google::protobuf::ArenaOptions;

namespace {

alignas(16) char arena_block[64 * 1024];

/* An arena per parse over a reused first block: no heap traffic in the loop. */
ArenaOptions
arena_options()
{
	ArenaOptions o;
	o.initial_block = arena_block;
	o.initial_block_size = sizeof arena_block;
	return o;
}

/* Before protobuf 22, Arena::Create left the message's own fields on the heap. */
template <typename T>
T *
create(Arena *arena)
{
#if GOOGLE_PROTOBUF_VERSION < 4022000
	return Arena::CreateMessage<T>(arena);
#else
	return Arena::Create<T>(arena);
#endif
}

void
bench_deep(const uint8_t *data, size_t len, size_t n)
{
	volatile int64_t sink = 0;
	uint8_t out[4096];

	benchmark::DeepNested msg;
	if (!msg.ParseFromArray(data, (int)len)) {
		fprintf(stderr, "cpp: cannot parse deep_nested\n");
		exit(1);
	}
	BENCH("deep", "marshal", n, sink = msg.SerializeToArray(out, (int)sizeof out));

	BENCH("deep", "unmarshal", n, {
		Arena arena(arena_options());
		auto *m = create<benchmark::DeepNested>(&arena);
		sink = m->ParseFromArray(data, (int)len);
	});

	BENCH("deep", "lazy", n, {
		Arena arena(arena_options());
		auto *m = create<benchmark::DeepNested>(&arena);
		sink = m->ParseFromArray(data, (int)len);
		sink = m->root_id();
		sink = (int64_t)m->root_name().size();
		sink = m->active();
	});

	BENCH("deep", "deep", n, {
		Arena arena(arena_options());
		auto *m = create<benchmark::DeepNested>(&arena);
		sink = m->ParseFromArray(data, (int)len);
		sink = m->root_id();
		sink = m->nested().id();
		sink = m->nested().nested().id();
		sink = m->nested().nested().nested().id();
		sink = m->nested().nested().nested().nested().value();
	});
	(void)sink;
}

void
bench_golden(const uint8_t *data, size_t len, size_t n)
{
	volatile int64_t sink = 0;
	uint8_t out[4096];

	protobuf_unittest::TestAllTypes msg;
	if (!msg.ParseFromArray(data, (int)len)) {
		fprintf(stderr, "cpp: cannot parse golden_message\n");
		exit(1);
	}
	msg.DiscardUnknownFields();
	BENCH("golden", "marshal", n, {
		int32_t i = (int32_t)(bench_i & 1023);
		msg.set_optional_int32(101 + i);
		msg.mutable_optional_nested_message()->set_bb(118 + i);
		msg.mutable_repeated_nested_message(0)->set_bb(218 + i);
		msg.mutable_repeated_nested_message(1)->set_bb(318 + i);
		msg.set_oneof_uint32((uint32_t)(601 + i));
		sink = msg.SerializeToArray(out, (int)sizeof out);
	});

	BENCH("golden", "unmarshal", n, {
		Arena arena(arena_options());
		auto *m = create<protobuf_unittest::TestAllTypes>(&arena);
		sink = m->ParseFromArray(data, (int)len);
	});

	BENCH("golden", "lazy", n, {
		Arena arena(arena_options());
		auto *m = create<protobuf_unittest::TestAllTypes>(&arena);
		sink = m->ParseFromArray(data, (int)len);
		sink = m->optional_int32();
		sink = m->optional_int64();
		sink = (int64_t)m->optional_string().size();
	});

	BENCH("golden", "deep", n, {
		Arena arena(arena_options());
		auto *m = create<protobuf_unittest::TestAllTypes>(&arena);
		sink = m->ParseFromArray(data, (int)len);
		sink = m->optional_int32();
		sink = (int64_t)m->optional_string().size();
		sink = m->optional_nested_message().bb();
		sink = m->optional_foreign_message().c();
		sink = m->optional_import_message().d();
	});
	(void)sink;
}

} // namespace

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
	return 0;
}

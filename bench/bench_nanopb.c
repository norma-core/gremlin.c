#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pb_decode.h>
#include <pb_encode.h>

#include "bench.h"
#include "benchmark.pb.h"

/* pb_realloc and pb_free land here: a bump pool reset before every decode. */
static _Alignas(16) uint8_t pool[256 * 1024];
static size_t pool_used;

/* A block: 16 bytes holding the size asked for, then the data. */
static size_t
block_bytes(size_t size)
{
	return ((size + 15u) & ~(size_t)15u) + 16u;
}

void *
bench_realloc(void *p, size_t size)
{
	size_t old = p != NULL ? ((size_t *)p)[-1] : 0;
	uint8_t *blk = pool + pool_used;
	/* The last block grows in place, as realloc usually manages for an array. */
	if (p != NULL && (uint8_t *)p - 16 + block_bytes(old) == blk)
		blk = (uint8_t *)p - 16;
	if ((size_t)(blk - pool) + block_bytes(size) > sizeof pool) {
		fprintf(stderr, "nanopb: pool exhausted\n");
		exit(1);
	}
	pool_used = (size_t)(blk - pool) + block_bytes(size);
	((size_t *)(blk + 16))[-1] = size;
	if (old != 0 && blk + 16 != (uint8_t *)p)
		memcpy(blk + 16, p, old < size ? old : size);
	return blk + 16;
}

void
bench_free(void *p)
{
	(void)p;
}

static int
decode(benchmark_DeepNested *m, const uint8_t *data, size_t len)
{
	pool_used = 0;
	pb_istream_t s = pb_istream_from_buffer(data, len);
	return pb_decode(&s, benchmark_DeepNested_fields, m);
}

static void
bench_deep(const uint8_t *data, size_t len, size_t n)
{
	volatile int64_t sink = 0;
	uint8_t out[4096];
	benchmark_DeepNested msg, m;

	if (!decode(&msg, data, len)) {
		fprintf(stderr, "nanopb: cannot parse deep_nested\n");
		exit(1);
	}
	pb_ostream_t check = pb_ostream_from_buffer(out, sizeof out);
	if (!pb_encode(&check, benchmark_DeepNested_fields, &msg) ||
	    check.bytes_written != len || memcmp(out, data, len) != 0) {
		fprintf(stderr, "nanopb: deep_nested does not round-trip\n");
		exit(1);
	}
	BENCH("deep", "marshal", n, {
		pb_ostream_t s = pb_ostream_from_buffer(out, sizeof out);
		sink = pb_encode(&s, benchmark_DeepNested_fields, &msg);
	});

	/* From here on every decode reuses the pool, msg's items included. */
	BENCH("deep", "unmarshal", n, sink = decode(&m, data, len));

	BENCH("deep", "lazy", n, {
		sink = decode(&m, data, len);
		sink = m.root_id;
		sink = (int64_t)strlen(m.root_name);
		sink = m.active;
	});

	BENCH("deep", "deep", n, {
		sink = decode(&m, data, len);
		sink = m.root_id;
		sink = m.nested.id;
		sink = m.nested.nested.id;
		sink = m.nested.nested.nested.id;
		sink = m.nested.nested.nested.nested.value;
	});
	(void)sink;
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

	/* Walk golden_message with nanopb's own field skipper: it stops at the groups. */
	pb_istream_t s = pb_istream_from_buffer(golden, golden_len);
	pb_wire_type_t wt;
	uint32_t tag;
	bool eof = false;
	while (pb_decode_tag(&s, &wt, &tag, &eof)) {
		if (!pb_skip_field(&s, wt)) {
			fprintf(stderr, "nanopb: cannot parse golden_message (%s), skipped\n", PB_GET_ERROR(&s));
			goto out;
		}
	}
	if (!eof) {
		fprintf(stderr, "nanopb: cannot parse golden_message (%s), skipped\n", PB_GET_ERROR(&s));
		goto out;
	}
	fprintf(stderr, "nanopb: golden_message is readable, but has no benchmark here\n");
	free(deep);
	free(golden);
	return 1;
out:
	free(deep);
	free(golden);
	return 0;
}

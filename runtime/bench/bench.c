/*
 * bench.c — microbenchmarks for the production functions in gremlin.h.
 *
 * Usage:
 *   ./bench                     # run all benches, 30s per measurement
 *   ./bench FILTER              # run only benches whose name contains FILTER
 *   ./bench FILTER SECONDS      # ... with a custom per-measurement duration
 *   ./bench all 10              # "all" is an alias for empty filter
 *
 * Available bench names: varint_size, varint_encode, varint_decode,
 *                        fixed32, fixed64
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gremlin.h"

enum { N = 1024 };

static uint64_t
xorshift64(uint64_t *s)
{
	uint64_t x = *s;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	*s = x;
	return x;
}

/*
 * Time-gated bench state. Caller wraps a tight inner loop between
 * bench_start and bench_tick, calling bench_tick after each full pass;
 * loop until bench_tick returns > 0 (seconds reached), then call
 * bench_ns_per_call.
 */
struct bench_timer {
	struct timespec t0, t1;
	size_t          calls;
	double          elapsed;
	double          target;
};

static inline void
bench_start(struct bench_timer *b, double target_seconds)
{
	b->calls = 0;
	b->elapsed = 0.0;
	b->target = target_seconds;
	clock_gettime(CLOCK_MONOTONIC, &b->t0);
}

static inline int
bench_tick(struct bench_timer *b, size_t pass_calls)
{
	b->calls += pass_calls;
	clock_gettime(CLOCK_MONOTONIC, &b->t1);
	b->elapsed = (double)(b->t1.tv_sec - b->t0.tv_sec)
		+ (double)(b->t1.tv_nsec - b->t0.tv_nsec) / 1e9;
	return b->elapsed >= b->target;
}

static inline double
bench_ns_per_call(const struct bench_timer *b)
{
	return (b->elapsed * 1e9) / (double)b->calls;
}

/* Trampolines — always_inline functions can't be address-of'd through a
 * function pointer without going through a real function boundary. */
static size_t tramp_varint_size(uint64_t v)         { return gremlin_varint_size(v); }
static size_t tramp_varint_size_cascade(uint64_t v) { return gremlin_varint_size_cascade(v); }

static double
ns_per_size(size_t (*fn)(uint64_t), const uint64_t *inputs, size_t n, double seconds)
{
	volatile size_t sink = 0;
	struct bench_timer t;
	bench_start(&t, seconds);
	do {
		for (size_t i = 0; i < n; i++) sink += fn(inputs[i]);
	} while (!bench_tick(&t, n));
	(void)sink;
	return bench_ns_per_call(&t);
}

/* ---- varint_size ---- */

static void
bench_varint_size(double seconds)
{
	uint64_t *inputs = malloc(sizeof(uint64_t) * N);
	uint64_t s;

	printf("--- varint_size ---\n");

	#define SIZE_ROW(label)  \
		printf("  %-18s lib=%6.2fns  cascade=%6.2fns\n", label, \
			ns_per_size(tramp_varint_size,         inputs, N, seconds), \
			ns_per_size(tramp_varint_size_cascade, inputs, N, seconds))

	for (size_t i = 0; i < N; i++) inputs[i] = (uint64_t)(i & 0x3f);
	SIZE_ROW("tiny (0..63)");

	s = 1;
	for (size_t i = 0; i < N; i++) inputs[i] = xorshift64(&s) & 0xffffffffULL;
	SIZE_ROW("u32 random");

	s = 2;
	for (size_t i = 0; i < N; i++) inputs[i] = xorshift64(&s);
	SIZE_ROW("u64 random");

	s = 3;
	for (size_t i = 0; i < N; i++) inputs[i] = xorshift64(&s) | 0x8000000000000000ULL;
	SIZE_ROW("u64 msb set (10B)");

	#undef SIZE_ROW
	free(inputs);
}

/* ---- varint_encode ---- */

static double
varint_encode_ns(const uint64_t *inputs, size_t n, double seconds)
{
	enum { MAX_PER = 10 };
	uint8_t *buf = malloc(n * MAX_PER);
	volatile size_t sink = 0;
	struct bench_timer t;
	bench_start(&t, seconds);
	do {
		struct gremlin_writer w;
		gremlin_writer_init(&w, buf, n * MAX_PER);
		for (size_t i = 0; i < n; i++) gremlin_varint_encode(&w, inputs[i]);
		sink += w.offset;
	} while (!bench_tick(&t, n));
	(void)sink;
	free(buf);
	return bench_ns_per_call(&t);
}

static void
bench_varint_encode(double seconds)
{
	uint64_t *inputs = malloc(sizeof(uint64_t) * N);
	uint64_t s;

	printf("--- varint_encode ---\n");
	#define ENC_ROW(label)  \
		printf("  %-18s %6.2fns\n", label, varint_encode_ns(inputs, N, seconds))

	for (size_t i = 0; i < N; i++) inputs[i] = (uint64_t)(i & 0x3f);
	ENC_ROW("tiny (0..63)");

	s = 1;
	for (size_t i = 0; i < N; i++) inputs[i] = xorshift64(&s) & 0xffffffffULL;
	ENC_ROW("u32 random");

	s = 2;
	for (size_t i = 0; i < N; i++) inputs[i] = xorshift64(&s);
	ENC_ROW("u64 random");

	s = 3;
	for (size_t i = 0; i < N; i++) inputs[i] = xorshift64(&s) | 0x8000000000000000ULL;
	ENC_ROW("u64 msb set (10B)");

	#undef ENC_ROW
	free(inputs);
}

/* ---- varint_decode ---- */

static size_t
manual_varint_encode(uint8_t *out, uint64_t v)
{
	size_t n = 0;
	while (v >= 0x80u) {
		out[n++] = (uint8_t)((v & 0x7Fu) | 0x80u);
		v >>= 7;
	}
	out[n++] = (uint8_t)v;
	return n;
}

static double
varint_decode_ns(const uint64_t *inputs, size_t n, double seconds)
{
	enum { MAX_PER = 10 };
	uint8_t *buf = malloc(n * MAX_PER);
	size_t  *offsets = malloc(sizeof(size_t) * n);
	size_t   total = 0;
	for (size_t i = 0; i < n; i++) {
		offsets[i] = total;
		total += manual_varint_encode(buf + total, inputs[i]);
	}

	volatile uint64_t sink = 0;
	struct bench_timer t;
	bench_start(&t, seconds);
	do {
		for (size_t i = 0; i < n; i++) {
			size_t off = offsets[i];
			struct gremlin_varint_decode_result d =
				gremlin_varint_decode(buf + off, total - off);
			sink += d.value + d.consumed;
		}
	} while (!bench_tick(&t, n));
	(void)sink;
	free(buf);
	free(offsets);
	return bench_ns_per_call(&t);
}

static void
bench_varint_decode(double seconds)
{
	uint64_t *inputs = malloc(sizeof(uint64_t) * N);
	uint64_t s;

	printf("--- varint_decode ---\n");
	#define DEC_ROW(label)  \
		printf("  %-18s %6.2fns\n", label, varint_decode_ns(inputs, N, seconds))

	for (size_t i = 0; i < N; i++) inputs[i] = (uint64_t)(i & 0x3f);
	DEC_ROW("tiny (0..63)");

	s = 1;
	for (size_t i = 0; i < N; i++) inputs[i] = xorshift64(&s) & 0xffffffffULL;
	DEC_ROW("u32 random");

	s = 2;
	for (size_t i = 0; i < N; i++) inputs[i] = xorshift64(&s);
	DEC_ROW("u64 random");

	s = 3;
	for (size_t i = 0; i < N; i++) inputs[i] = xorshift64(&s) | 0x8000000000000000ULL;
	DEC_ROW("u64 msb set (10B)");

	#undef DEC_ROW
	free(inputs);
}

/* ---- fixed32 / fixed64 ---- */

static void
bench_fixed32(double seconds)
{
	enum { N32 = 4096 };
	uint32_t *inputs = malloc(sizeof(uint32_t) * N32);
	uint8_t  *buf    = malloc(N32 * 4);
	uint64_t s = 11;
	for (size_t i = 0; i < N32; i++) {
		inputs[i] = (uint32_t)xorshift64(&s);
	}

	volatile size_t sink_enc = 0;
	struct bench_timer te;
	bench_start(&te, seconds);
	do {
		struct gremlin_writer w;
		gremlin_writer_init(&w, buf, N32 * 4);
		for (size_t i = 0; i < N32; i++) gremlin_fixed32_encode(&w, inputs[i]);
		sink_enc += w.offset;
	} while (!bench_tick(&te, N32));
	(void)sink_enc;
	double enc_ns = bench_ns_per_call(&te);

	volatile uint32_t sink_dec = 0;
	struct bench_timer td;
	bench_start(&td, seconds);
	do {
		for (size_t i = 0; i < N32; i++) {
			sink_dec += gremlin_fixed32_decode(buf + i * 4, 4).value;
		}
	} while (!bench_tick(&td, N32));
	(void)sink_dec;
	double dec_ns = bench_ns_per_call(&td);

	printf("--- fixed32 (u32 random) ---\n");
	printf("  encode %6.2fns   decode %6.2fns\n", enc_ns, dec_ns);

	free(inputs); free(buf);
}

static void
bench_fixed64(double seconds)
{
	uint64_t *inputs = malloc(sizeof(uint64_t) * N);
	uint8_t  *buf    = malloc(N * 8);
	uint64_t s = 13;
	for (size_t i = 0; i < N; i++) inputs[i] = xorshift64(&s);

	volatile size_t sink_enc = 0;
	struct bench_timer te;
	bench_start(&te, seconds);
	do {
		struct gremlin_writer w;
		gremlin_writer_init(&w, buf, N * 8);
		for (size_t i = 0; i < N; i++) gremlin_fixed64_encode(&w, inputs[i]);
		sink_enc += w.offset;
	} while (!bench_tick(&te, N));
	(void)sink_enc;
	double enc_ns = bench_ns_per_call(&te);

	volatile uint64_t sink_dec = 0;
	struct bench_timer td;
	bench_start(&td, seconds);
	do {
		for (size_t i = 0; i < N; i++) {
			sink_dec += gremlin_fixed64_decode(buf + i * 8, 8).value;
		}
	} while (!bench_tick(&td, N));
	(void)sink_dec;
	double dec_ns = bench_ns_per_call(&td);

	printf("--- fixed64 (u64 random) ---\n");
	printf("  encode %6.2fns   decode %6.2fns\n", enc_ns, dec_ns);

	free(inputs); free(buf);
}

/* ---- tag ---- */

static void
bench_tag(double seconds)
{
	/* Input distributions. `fn[i]` is the field number, `wt[i]` the
	 * wire type (0..5). Small field numbers with common wire types are
	 * the overwhelming real-world case (field tag 1-15 + VARINT/LEN
	 * gives a 1-byte tag), so that's the hot-path distribution. */
	uint32_t *fn = malloc(sizeof(uint32_t) * N);
	uint8_t  *wt = malloc(N);
	uint8_t  *buf = malloc(N * 10);

	#define TAG_ENC_DEC(name, fn_init)                                           \
	do {                                                                         \
		uint64_t s = 101;                                                    \
		(void)s;                                                             \
		fn_init;                                                             \
		for (size_t i = 0; i < N; i++) wt[i] = (uint8_t)(i % 6);             \
		                                                                     \
		/* pre-encode into buf for the decode pass */                        \
		size_t *offsets = malloc(sizeof(size_t) * N);                        \
		struct gremlin_writer pw;                                            \
		gremlin_writer_init(&pw, buf, N * 10);                               \
		for (size_t i = 0; i < N; i++) {                                     \
			offsets[i] = pw.offset;                                      \
			gremlin_tag_encode(&pw, fn[i], (enum gremlin_wire_type)wt[i]); \
		}                                                                    \
		size_t total = pw.offset;                                            \
		                                                                     \
		/* tag encode */                                                     \
		volatile size_t sink_enc = 0;                                        \
		struct bench_timer te;                                               \
		bench_start(&te, seconds);                                           \
		do {                                                                 \
			struct gremlin_writer w;                                     \
			gremlin_writer_init(&w, buf, N * 10);                        \
			for (size_t i = 0; i < N; i++) {                             \
				gremlin_tag_encode(&w, fn[i], (enum gremlin_wire_type)wt[i]); \
			}                                                            \
			sink_enc += w.offset;                                        \
		} while (!bench_tick(&te, N));                                       \
		(void)sink_enc;                                                      \
		                                                                     \
		/* raw varint encode on the same packed value — baseline            \
		 * showing tag's overhead is ~nothing (tag_encode is a thin         \
		 * wrapper around varint_encode). */                                 \
		volatile size_t sink_enc_v = 0;                                      \
		struct bench_timer tev;                                              \
		bench_start(&tev, seconds);                                          \
		do {                                                                 \
			struct gremlin_writer w;                                     \
			gremlin_writer_init(&w, buf, N * 10);                        \
			for (size_t i = 0; i < N; i++) {                             \
				gremlin_varint_encode(&w,                            \
					((uint64_t)fn[i] * 8u) + (uint64_t)wt[i]);   \
			}                                                            \
			sink_enc_v += w.offset;                                      \
		} while (!bench_tick(&tev, N));                                      \
		(void)sink_enc_v;                                                    \
		                                                                     \
		/* tag decode */                                                     \
		volatile uint32_t sink_dec = 0;                                      \
		struct bench_timer td;                                               \
		bench_start(&td, seconds);                                           \
		do {                                                                 \
			for (size_t i = 0; i < N; i++) {                             \
				struct gremlin_tag_decode_result d =                 \
					gremlin_tag_decode(buf + offsets[i],         \
					                   total - offsets[i]);      \
				sink_dec += d.tag.field_num + (uint32_t)d.tag.wire_type; \
			}                                                            \
		} while (!bench_tick(&td, N));                                       \
		(void)sink_dec;                                                      \
		                                                                     \
		/* raw varint decode — shows the cost of tag's extra splits +       \
		 * validation (wire_type reject, field_num bounds check). */         \
		volatile uint64_t sink_dec_v = 0;                                    \
		struct bench_timer tdv;                                              \
		bench_start(&tdv, seconds);                                          \
		do {                                                                 \
			for (size_t i = 0; i < N; i++) {                             \
				struct gremlin_varint_decode_result d =              \
					gremlin_varint_decode(buf + offsets[i],      \
					                      total - offsets[i]);   \
				sink_dec_v += d.value + d.consumed;                  \
			}                                                            \
		} while (!bench_tick(&tdv, N));                                      \
		(void)sink_dec_v;                                                    \
		                                                                     \
		printf("  %-24s tag_enc=%6.2fns  u_enc=%6.2fns   tag_dec=%6.2fns  u_dec=%6.2fns\n", \
			name,                                                        \
			bench_ns_per_call(&te), bench_ns_per_call(&tev),             \
			bench_ns_per_call(&td), bench_ns_per_call(&tdv));            \
		free(offsets);                                                       \
	} while (0)

	printf("--- tag ---\n");

	/* 1: common small field numbers (1..15) — real protobuf default. */
	TAG_ENC_DEC("fn 1..15",
		for (size_t i = 0; i < N; i++) fn[i] = (uint32_t)(1 + (i % 15)));

	/* 2: 2-byte tag band (16..2047). */
	TAG_ENC_DEC("fn 16..2047",
		for (size_t i = 0; i < N; i++) fn[i] = (uint32_t)(16 + (xorshift64(&s) % (2048 - 16))));

	/* 3: full legal field-number range, random. */
	TAG_ENC_DEC("fn 1..2^29-1",
		for (size_t i = 0; i < N; i++) {
			uint32_t v = (uint32_t)(xorshift64(&s) % GREMLIN_MAX_FIELD_NUM);
			if (v == 0) v = 1;
			fn[i] = v;
		});

	#undef TAG_ENC_DEC
	free(fn); free(wt); free(buf);
}

/* ---- dispatcher ---- */

struct bench_case {
	const char *name;
	void      (*run)(double seconds);
};

static const struct bench_case BENCHES[] = {
	{ "varint_size",   bench_varint_size   },
	{ "varint_encode", bench_varint_encode },
	{ "varint_decode", bench_varint_decode },
	{ "fixed32",       bench_fixed32       },
	{ "fixed64",       bench_fixed64       },
	{ "tag",           bench_tag           },
};
#define N_BENCHES (sizeof(BENCHES) / sizeof(BENCHES[0]))

static void
print_usage(const char *prog)
{
	fprintf(stderr, "usage: %s [filter|all] [seconds]\n", prog);
	fprintf(stderr, "\navailable benches:\n");
	for (size_t i = 0; i < N_BENCHES; i++) {
		fprintf(stderr, "  %s\n", BENCHES[i].name);
	}
}

int
main(int argc, char **argv)
{
	const char *filter = (argc > 1) ? argv[1] : "";
	double seconds = (argc > 2) ? atof(argv[2]) : 30.0;

	if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
		print_usage(argv[0]);
		return 0;
	}
	if (strcmp(filter, "all") == 0) filter = "";

	size_t ran = 0;
	for (size_t i = 0; i < N_BENCHES; i++) {
		if (*filter != '\0' && strstr(BENCHES[i].name, filter) == NULL) {
			continue;
		}
		if (ran > 0) printf("\n");
		printf("# %s (%.0fs per measurement)\n", BENCHES[i].name, seconds);
		BENCHES[i].run(seconds);
		ran++;
	}

	if (ran == 0) {
		fprintf(stderr, "no bench matched filter: %s\n", filter);
		print_usage(argv[0]);
		return 1;
	}
	return 0;
}

/*
 * float_test.c — runtime sanity for gremlin_f32/f64 bit-cast.
 *
 * We prove the bit-cast functions are memory-safe + pure via ACSL, but
 * WP cannot validate the SEMANTIC claim: that the extracted bits are
 * the IEEE-754 binary representation. Union punning maps the float
 * object bytes to uint32 bytes, but C doesn't specify what those
 * bytes are — that's platform ABI (IEEE-754 on every sane target).
 *
 * This file asserts the expected bit patterns for a handful of
 * canonical floats + round-trip identity over a fuzzing corpus. If
 * any of these fails, the build/platform isn't IEEE-754 compatible
 * and the library can't be used as-is.
 */

#define _POSIX_C_SOURCE 199309L
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "gremlin.h"

#define CHECK_EQ(a, b) do { \
	if ((a) != (b)) { \
		fprintf(stderr, "FAIL %s:%d  " #a " != " #b "  (%llu vs %llu)\n", \
			__FILE__, __LINE__, \
			(unsigned long long)(a), (unsigned long long)(b)); \
		return 1; \
	} \
} while (0)

static int
test_f32_ieee754(void)
{
	/* Canonical IEEE-754 single-precision bit patterns. */
	CHECK_EQ(gremlin_f32_bits(0.0f),            0x00000000u);
	CHECK_EQ(gremlin_f32_bits(-0.0f),           0x80000000u);
	CHECK_EQ(gremlin_f32_bits(1.0f),            0x3F800000u);
	CHECK_EQ(gremlin_f32_bits(-1.0f),           0xBF800000u);
	CHECK_EQ(gremlin_f32_bits(2.0f),            0x40000000u);
	CHECK_EQ(gremlin_f32_bits(0.5f),            0x3F000000u);
	CHECK_EQ(gremlin_f32_bits(INFINITY),        0x7F800000u);
	CHECK_EQ(gremlin_f32_bits(-INFINITY),       0xFF800000u);

	/* from_bits reverses. */
	CHECK_EQ((unsigned)(gremlin_f32_from_bits(0x00000000u) == 0.0f),  1u);
	CHECK_EQ((unsigned)(gremlin_f32_from_bits(0x3F800000u) == 1.0f),  1u);
	CHECK_EQ((unsigned)(gremlin_f32_from_bits(0xBF800000u) == -1.0f), 1u);
	return 0;
}

static int
test_f64_ieee754(void)
{
	CHECK_EQ(gremlin_f64_bits(0.0),       0x0000000000000000ull);
	CHECK_EQ(gremlin_f64_bits(-0.0),      0x8000000000000000ull);
	CHECK_EQ(gremlin_f64_bits(1.0),       0x3FF0000000000000ull);
	CHECK_EQ(gremlin_f64_bits(-1.0),      0xBFF0000000000000ull);
	CHECK_EQ(gremlin_f64_bits(2.0),       0x4000000000000000ull);
	CHECK_EQ(gremlin_f64_bits(0.5),       0x3FE0000000000000ull);
	CHECK_EQ(gremlin_f64_bits(INFINITY),  0x7FF0000000000000ull);
	CHECK_EQ(gremlin_f64_bits(-INFINITY), 0xFFF0000000000000ull);

	CHECK_EQ((unsigned)(gremlin_f64_from_bits(0x0000000000000000ull) == 0.0),  1u);
	CHECK_EQ((unsigned)(gremlin_f64_from_bits(0x3FF0000000000000ull) == 1.0),  1u);
	CHECK_EQ((unsigned)(gremlin_f64_from_bits(0xBFF0000000000000ull) == -1.0), 1u);
	return 0;
}

static uint64_t
xs64(uint64_t *s)
{
	uint64_t x = *s;
	x ^= x << 13; x ^= x >> 7; x ^= x << 17;
	*s = x;
	return x;
}

/* Round-trip: f32_from_bits(f32_bits(x)) must return the exact same
 * bit pattern for every input (including NaN — the bits are preserved
 * even though NaN != NaN under float equality). */
static int
test_f32_roundtrip(void)
{
	uint64_t s = 0xA5A5;
	for (int i = 0; i < 1000000; i++) {
		uint32_t bits = (uint32_t)xs64(&s);
		float    f    = gremlin_f32_from_bits(bits);
		uint32_t back = gremlin_f32_bits(f);
		if (bits != back) {
			fprintf(stderr, "FAIL f32 roundtrip: 0x%08x -> %f -> 0x%08x\n",
				bits, (double)f, back);
			return 1;
		}
	}
	return 0;
}

static int
test_f64_roundtrip(void)
{
	uint64_t s = 0xC3C3;
	for (int i = 0; i < 1000000; i++) {
		uint64_t bits = xs64(&s);
		double   d    = gremlin_f64_from_bits(bits);
		uint64_t back = gremlin_f64_bits(d);
		if (bits != back) {
			fprintf(stderr, "FAIL f64 roundtrip: 0x%016llx\n",
				(unsigned long long)bits);
			return 1;
		}
	}
	return 0;
}

int
main(void)
{
	if (test_f32_ieee754())    return 1;
	if (test_f64_ieee754())    return 1;
	if (test_f32_roundtrip())  return 1;
	if (test_f64_roundtrip())  return 1;
	printf("float_test: OK  (8 IEEE-754 constants per width + 1M round-trips each)\n");
	return 0;
}

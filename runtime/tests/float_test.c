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
#include <math.h>
#include <stdint.h>

#include "gremlin.h"
#include "tests.h"

TEST(test_f32_ieee754)
{
	/* Canonical IEEE-754 single-precision bit patterns. */
	ASSERT_EQ(0x00000000u, gremlin_f32_bits(0.0f));
	ASSERT_EQ(0x80000000u, gremlin_f32_bits(-0.0f));
	ASSERT_EQ(0x3F800000u, gremlin_f32_bits(1.0f));
	ASSERT_EQ(0xBF800000u, gremlin_f32_bits(-1.0f));
	ASSERT_EQ(0x40000000u, gremlin_f32_bits(2.0f));
	ASSERT_EQ(0x3F000000u, gremlin_f32_bits(0.5f));
	ASSERT_EQ(0x7F800000u, gremlin_f32_bits(INFINITY));
	ASSERT_EQ(0xFF800000u, gremlin_f32_bits(-INFINITY));

	/* from_bits reverses. */
	ASSERT_TRUE(gremlin_f32_from_bits(0x00000000u) ==  0.0f);
	ASSERT_TRUE(gremlin_f32_from_bits(0x3F800000u) ==  1.0f);
	ASSERT_TRUE(gremlin_f32_from_bits(0xBF800000u) == -1.0f);
}

TEST(test_f64_ieee754)
{
	ASSERT_EQ(0x0000000000000000ull, gremlin_f64_bits(0.0));
	ASSERT_EQ(0x8000000000000000ull, gremlin_f64_bits(-0.0));
	ASSERT_EQ(0x3FF0000000000000ull, gremlin_f64_bits(1.0));
	ASSERT_EQ(0xBFF0000000000000ull, gremlin_f64_bits(-1.0));
	ASSERT_EQ(0x4000000000000000ull, gremlin_f64_bits(2.0));
	ASSERT_EQ(0x3FE0000000000000ull, gremlin_f64_bits(0.5));
	ASSERT_EQ(0x7FF0000000000000ull, gremlin_f64_bits(INFINITY));
	ASSERT_EQ(0xFFF0000000000000ull, gremlin_f64_bits(-INFINITY));

	ASSERT_TRUE(gremlin_f64_from_bits(0x0000000000000000ull) ==  0.0);
	ASSERT_TRUE(gremlin_f64_from_bits(0x3FF0000000000000ull) ==  1.0);
	ASSERT_TRUE(gremlin_f64_from_bits(0xBFF0000000000000ull) == -1.0);
}

/* Simple xorshift64 — enough entropy for bit-pattern fuzzing, no need
 * for cryptographic randomness. */
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
TEST(test_f32_roundtrip)
{
	uint64_t s = 0xA5A5;
	for (int i = 0; i < 1000000; i++) {
		uint32_t bits = (uint32_t)xs64(&s);
		float    f    = gremlin_f32_from_bits(bits);
		uint32_t back = gremlin_f32_bits(f);
		ASSERT_EQ(bits, back);
	}
}

TEST(test_f64_roundtrip)
{
	uint64_t s = 0xC3C3;
	for (int i = 0; i < 1000000; i++) {
		uint64_t bits = xs64(&s);
		double   d    = gremlin_f64_from_bits(bits);
		uint64_t back = gremlin_f64_bits(d);
		ASSERT_EQ(bits, back);
	}
}

int
main(void)
{
	printf("Running runtime tests...\n\n");

	RUN_TEST(test_f32_ieee754);
	RUN_TEST(test_f64_ieee754);
	RUN_TEST(test_f32_roundtrip);
	RUN_TEST(test_f64_roundtrip);

	printf("\nAll tests passed!\n");
	return 0;
}

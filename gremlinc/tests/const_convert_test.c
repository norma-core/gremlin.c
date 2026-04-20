#include <float.h>
#include <math.h>

#include "tests.h"
#include "gremlinc/const_convert.h"

/*
 * Test-level verification for const_convert. ACSL contracts on the module
 * document intent; this file covers the three behavioural corners:
 *   - valid conversions produce the right value
 *   - kind mismatches return GREMLINP_ERROR_INVALID_FIELD_VALUE
 *   - range overflows return GREMLINP_ERROR_OVERFLOW
 */

static struct gremlinp_const_parse_result
make_int(int64_t v)
{
	struct gremlinp_const_parse_result r;
	r.kind = GREMLINP_CONST_INT;
	r.u.int_value = v;
	r.error = GREMLINP_OK;
	return r;
}

static struct gremlinp_const_parse_result
make_uint(uint64_t v)
{
	struct gremlinp_const_parse_result r;
	r.kind = GREMLINP_CONST_UINT;
	r.u.uint_value = v;
	r.error = GREMLINP_OK;
	return r;
}

static struct gremlinp_const_parse_result
make_float(double v)
{
	struct gremlinp_const_parse_result r;
	r.kind = GREMLINP_CONST_FLOAT;
	r.u.float_value = v;
	r.error = GREMLINP_OK;
	return r;
}

static struct gremlinp_const_parse_result
make_ident(const char *span, size_t len)
{
	struct gremlinp_const_parse_result r;
	r.kind = GREMLINP_CONST_IDENTIFIER;
	r.u.span.start = span;
	r.u.span.length = len;
	r.u.span.error = GREMLINP_OK;
	r.error = GREMLINP_OK;
	return r;
}

static struct gremlinp_const_parse_result
make_string(void)
{
	struct gremlinp_const_parse_result r;
	r.kind = GREMLINP_CONST_STRING;
	r.u.span.start = "foo";
	r.u.span.length = 3;
	r.u.span.error = GREMLINP_OK;
	r.error = GREMLINP_OK;
	return r;
}

/* ---------------- int32 ---------------- */

static TEST(int32_accepts_positive_and_negative)
{
	struct gremlinp_const_parse_result c = make_int(42);
	struct gremlinc_int32_convert_result r = gremlinc_const_to_int32(&c);
	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(42, r.value);

	c = make_int(-7);
	r = gremlinc_const_to_int32(&c);
	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(-7, r.value);

	c = make_uint(100);
	r = gremlinc_const_to_int32(&c);
	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(100, r.value);
}

static TEST(int32_overflow_rejected)
{
	struct gremlinp_const_parse_result c = make_int((int64_t)INT32_MAX + 1);
	struct gremlinc_int32_convert_result r = gremlinc_const_to_int32(&c);
	ASSERT_EQ(GREMLINP_ERROR_OVERFLOW, r.error);

	c = make_int((int64_t)INT32_MIN - 1);
	r = gremlinc_const_to_int32(&c);
	ASSERT_EQ(GREMLINP_ERROR_OVERFLOW, r.error);

	c = make_uint((uint64_t)INT32_MAX + 1);
	r = gremlinc_const_to_int32(&c);
	ASSERT_EQ(GREMLINP_ERROR_OVERFLOW, r.error);
}

static TEST(int32_rejects_string_and_float)
{
	struct gremlinp_const_parse_result c = make_string();
	struct gremlinc_int32_convert_result r = gremlinc_const_to_int32(&c);
	ASSERT_EQ(GREMLINP_ERROR_INVALID_FIELD_VALUE, r.error);

	c = make_float(1.5);
	r = gremlinc_const_to_int32(&c);
	ASSERT_EQ(GREMLINP_ERROR_INVALID_FIELD_VALUE, r.error);
}

/* ---------------- uint32 ---------------- */

static TEST(uint32_accepts_int_and_uint)
{
	struct gremlinp_const_parse_result c = make_uint(0x80000000ULL);
	struct gremlinc_uint32_convert_result r = gremlinc_const_to_uint32(&c);
	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ((int64_t)0x80000000u, (int64_t)r.value);

	c = make_int(42);
	r = gremlinc_const_to_uint32(&c);
	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(42, r.value);
}

static TEST(uint32_rejects_negative_and_overflow)
{
	struct gremlinp_const_parse_result c = make_int(-1);
	struct gremlinc_uint32_convert_result r = gremlinc_const_to_uint32(&c);
	ASSERT_EQ(GREMLINP_ERROR_OVERFLOW, r.error);

	c = make_uint((uint64_t)UINT32_MAX + 1);
	r = gremlinc_const_to_uint32(&c);
	ASSERT_EQ(GREMLINP_ERROR_OVERFLOW, r.error);
}

/* ---------------- uint64 ---------------- */

static TEST(uint64_rejects_negative_int)
{
	struct gremlinp_const_parse_result c = make_int(-5);
	struct gremlinc_uint64_convert_result r = gremlinc_const_to_uint64(&c);
	ASSERT_EQ(GREMLINP_ERROR_OVERFLOW, r.error);

	c = make_uint(UINT64_MAX);
	r = gremlinc_const_to_uint64(&c);
	ASSERT_EQ(GREMLINP_OK, r.error);
}

/* ---------------- float/double ---------------- */

static TEST(double_accepts_all_numeric_kinds)
{
	struct gremlinp_const_parse_result c = make_float(3.14);
	struct gremlinc_double_convert_result r = gremlinc_const_to_double(&c);
	ASSERT_EQ(GREMLINP_OK, r.error);
	assert(r.value > 3.13 && r.value < 3.15);

	c = make_int(-10);
	r = gremlinc_const_to_double(&c);
	ASSERT_EQ(GREMLINP_OK, r.error);
	assert(r.value == -10.0);

	c = make_uint(100);
	r = gremlinc_const_to_double(&c);
	ASSERT_EQ(GREMLINP_OK, r.error);
	assert(r.value == 100.0);
}

static TEST(float_rejects_string)
{
	struct gremlinp_const_parse_result c = make_string();
	struct gremlinc_float_convert_result r = gremlinc_const_to_float(&c);
	ASSERT_EQ(GREMLINP_ERROR_INVALID_FIELD_VALUE, r.error);
}

/* ---------------- bool ---------------- */

static TEST(bool_accepts_true_false_identifiers)
{
	struct gremlinp_const_parse_result c = make_ident("true", 4);
	struct gremlinc_bool_convert_result r = gremlinc_const_to_bool(&c);
	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(true, r.value);

	c = make_ident("false", 5);
	r = gremlinc_const_to_bool(&c);
	ASSERT_EQ(GREMLINP_OK, r.error);
	ASSERT_EQ(false, r.value);
}

static TEST(bool_rejects_other_identifiers_and_ints)
{
	struct gremlinp_const_parse_result c = make_ident("yes", 3);
	struct gremlinc_bool_convert_result r = gremlinc_const_to_bool(&c);
	ASSERT_EQ(GREMLINP_ERROR_INVALID_FIELD_VALUE, r.error);

	c = make_int(1);
	r = gremlinc_const_to_bool(&c);
	ASSERT_EQ(GREMLINP_ERROR_INVALID_FIELD_VALUE, r.error);
}

void const_convert_test(void);

void
const_convert_test(void)
{
	RUN_TEST(int32_accepts_positive_and_negative);
	RUN_TEST(int32_overflow_rejected);
	RUN_TEST(int32_rejects_string_and_float);
	RUN_TEST(uint32_accepts_int_and_uint);
	RUN_TEST(uint32_rejects_negative_and_overflow);
	RUN_TEST(uint64_rejects_negative_int);
	RUN_TEST(double_accepts_all_numeric_kinds);
	RUN_TEST(float_rejects_string);
	RUN_TEST(bool_accepts_true_false_identifiers);
	RUN_TEST(bool_rejects_other_identifiers_and_ints);
}

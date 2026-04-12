#include "tests.h"
#include "gremlinp/lib.h"

void lexems_test(void);

static struct gremlinp_parser_buffer make_buf(char *str) {
    struct gremlinp_parser_buffer pb;
    gremlinp_parser_buffer_init(&pb, str, 0);
    return pb;
}

/* Value correctness tests — these cover the strtoll/strtod admits */

TEST(lexems_test_integer_values)
{
    char s1[] = "123";
    struct gremlinp_parser_buffer buf = make_buf(s1);
    struct gremlinp_int64_parse_result r = gremlinp_lexems_parse_integer_literal(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_EQ(123, r.value);

    char s2[] = "-42";
    buf = make_buf(s2);
    r = gremlinp_lexems_parse_integer_literal(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_EQ(-42, r.value);

    char s3[] = "0";
    buf = make_buf(s3);
    r = gremlinp_lexems_parse_integer_literal(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_EQ(0, r.value);

    char s4[] = "0x1F";
    buf = make_buf(s4);
    r = gremlinp_lexems_parse_integer_literal(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_EQ(31, r.value);

    char s5[] = "077";
    buf = make_buf(s5);
    r = gremlinp_lexems_parse_integer_literal(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_EQ(63, r.value);
}

TEST(lexems_test_uint_values)
{
    char s1[] = "18446744073709551615";
    struct gremlinp_parser_buffer buf = make_buf(s1);
    struct gremlinp_uint64_parse_result r = gremlinp_lexems_parse_uint_literal(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_TRUE(r.value == 18446744073709551615ULL);

    char s2[] = "0xFFFFFFFFFFFFFFFF";
    buf = make_buf(s2);
    r = gremlinp_lexems_parse_uint_literal(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_TRUE(r.value == 0xFFFFFFFFFFFFFFFFULL);
}

TEST(lexems_test_float_values)
{
    char s1[] = "3.14";
    struct gremlinp_parser_buffer buf = make_buf(s1);
    struct gremlinp_double_parse_result r = gremlinp_lexems_parse_float_literal(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_TRUE(r.value > 3.13 && r.value < 3.15);

    char s2[] = "-inf";
    buf = make_buf(s2);
    r = gremlinp_lexems_parse_float_literal(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_TRUE(r.value < -1e300);

    char s3[] = "nan";
    buf = make_buf(s3);
    r = gremlinp_lexems_parse_float_literal(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_TRUE(r.value != r.value); /* NaN != NaN */

    char s4[] = "1e10";
    buf = make_buf(s4);
    r = gremlinp_lexems_parse_float_literal(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_TRUE(r.value > 9e9 && r.value < 1.1e10);
}

TEST(lexems_test_const_value_priority)
{
    /* "123" should parse as INT, not FLOAT */
    char s1[] = "123";
    struct gremlinp_parser_buffer buf = make_buf(s1);
    struct gremlinp_const_parse_result r = gremlinp_lexems_parse_const_value(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_EQ(GREMLINP_CONST_INT, r.kind);
    ASSERT_EQ(123, r.u.int_value);

    /* "3.14" should parse as FLOAT */
    char s2[] = "3.14";
    buf = make_buf(s2);
    r = gremlinp_lexems_parse_const_value(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_EQ(GREMLINP_CONST_FLOAT, r.kind);

    /* "true" should parse as IDENTIFIER */
    char s3[] = "true";
    buf = make_buf(s3);
    r = gremlinp_lexems_parse_const_value(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_EQ(GREMLINP_CONST_IDENTIFIER, r.kind);

    /* quoted string */
    char s4[] = "\"hello\"";
    buf = make_buf(s4);
    r = gremlinp_lexems_parse_const_value(&buf);
    ASSERT_EQ(GREMLINP_OK, r.error);
    ASSERT_EQ(GREMLINP_CONST_STRING, r.kind);
}

void
lexems_test(void)
{
    RUN_TEST(lexems_test_integer_values);
    RUN_TEST(lexems_test_uint_values);
    RUN_TEST(lexems_test_float_values);
    RUN_TEST(lexems_test_const_value_priority);
}

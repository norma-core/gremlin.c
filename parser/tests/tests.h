#ifndef _TESTS_H_
#define _TESTS_H_

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Helper to extract basename from __FILE__ at compile time */
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

/* Test framework macros */
#define TEST(name) void name(void)

#define RUN_TEST(name) do { \
    printf("[%s] Running %s... ", __FILENAME__, #name); \
    name(); \
    printf("PASSED\n"); \
} while(0)

/* Assertion macros */
#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "\nAssertion failed: %s\n", #expr); \
            fprintf(stderr, "  File: %s, Line: %d\n", __FILE__, __LINE__); \
            assert(0); \
        } \
    } while(0)

#define ASSERT_FALSE(expr) \
    do { \
        if (expr) { \
            fprintf(stderr, "\nAssertion failed: !(%s)\n", #expr); \
            fprintf(stderr, "  File: %s, Line: %d\n", __FILE__, __LINE__); \
            assert(0); \
        } \
    } while(0)

#define ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            fprintf(stderr, "\nAssertion failed: %s == %s\n", #expected, #actual); \
            fprintf(stderr, "  Expected: %ld\n", (long)(expected)); \
            fprintf(stderr, "  Actual: %ld\n", (long)(actual)); \
            fprintf(stderr, "  File: %s, Line: %d\n", __FILE__, __LINE__); \
            assert(0); \
        } \
    } while(0)

#define ASSERT_STR_EQ(expected, actual) \
    do { \
        if (strcmp((expected), (actual)) != 0) { \
            fprintf(stderr, "\nAssertion failed: %s == %s\n", #expected, #actual); \
            fprintf(stderr, "  Expected: \"%s\"\n", (expected)); \
            fprintf(stderr, "  Actual: \"%s\"\n", (actual)); \
            fprintf(stderr, "  File: %s, Line: %d\n", __FILE__, __LINE__); \
            assert(0); \
        } \
    } while(0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            fprintf(stderr, "\nAssertion failed: %s != NULL\n", #ptr); \
            fprintf(stderr, "  File: %s, Line: %d\n", __FILE__, __LINE__); \
            assert(0); \
        } \
    } while(0)

#define ASSERT_NULL(ptr) \
    do { \
        if ((ptr) != NULL) { \
            fprintf(stderr, "\nAssertion failed: %s == NULL\n", #ptr); \
            fprintf(stderr, "  File: %s, Line: %d\n", __FILE__, __LINE__); \
            assert(0); \
        } \
    } while(0)

#define ASSERT_SPAN_EQ(expected_str, span_start, span_length) \
    do { \
        size_t _exp_len = strlen(expected_str); \
        if (_exp_len != (span_length) || memcmp((expected_str), (span_start), _exp_len) != 0) { \
            fprintf(stderr, "\nAssertion failed: span == \"%s\"\n", (expected_str)); \
            fprintf(stderr, "  Actual: \"%.*s\" (len %zu)\n", (int)(span_length), (span_start), (size_t)(span_length)); \
            fprintf(stderr, "  File: %s, Line: %d\n", __FILE__, __LINE__); \
            assert(0); \
        } \
    } while(0)

#define RUN_TEST_COUNTED(name) do { \
    RUN_TEST(name); \
    test_count++; \
} while(0)

#endif /* !_TESTS_H_ */

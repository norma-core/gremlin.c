#ifndef INTEGRATION_TEST_FRAMEWORK_H
#define INTEGRATION_TEST_FRAMEWORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Minimal test framework — no dependencies. Each test is a function
 * returning 0 on success, non-zero on failure. TEST_ASSERT_* macros
 * print a diagnostic and early-return.
 */

#define TEST_FAIL(fmt, ...) do { \
	fprintf(stderr, "  FAIL %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
	return 1; \
} while (0)

#define TEST_ASSERT(cond) do { \
	if (!(cond)) TEST_FAIL("assertion failed: %s", #cond); \
} while (0)

#define TEST_ASSERT_EQ_INT(expected, actual) do { \
	long long _e = (long long)(expected); \
	long long _a = (long long)(actual); \
	if (_e != _a) TEST_FAIL("expected %lld, got %lld (%s vs %s)", \
	                        _e, _a, #expected, #actual); \
} while (0)

#define TEST_ASSERT_EQ_UINT(expected, actual) do { \
	unsigned long long _e = (unsigned long long)(expected); \
	unsigned long long _a = (unsigned long long)(actual); \
	if (_e != _a) TEST_FAIL("expected %llu, got %llu (%s vs %s)", \
	                        _e, _a, #expected, #actual); \
} while (0)

#define TEST_ASSERT_EQ_FLOAT(expected, actual) do { \
	double _e = (double)(expected); \
	double _a = (double)(actual); \
	if (_e != _a) TEST_FAIL("expected %g, got %g", _e, _a); \
} while (0)

#define TEST_ASSERT_EQ_BOOL(expected, actual) do { \
	bool _e = (bool)(expected); \
	bool _a = (bool)(actual); \
	if (_e != _a) TEST_FAIL("expected %s, got %s", \
	                        _e ? "true" : "false", _a ? "true" : "false"); \
} while (0)

/* gremlin_bytes vs C string literal: compare bytes + exact length. */
#define TEST_ASSERT_EQ_STR(expected_cstr, actual_bytes) do { \
	const char *_e = (expected_cstr); \
	struct gremlin_bytes _a = (actual_bytes); \
	size_t _el = strlen(_e); \
	if (_a.len != _el || (_el > 0 && memcmp(_e, _a.data, _el) != 0)) { \
		TEST_FAIL("expected \"%s\" (len %zu), got len %zu", \
		          _e, _el, _a.len); \
	} \
} while (0)

/* Byte-for-byte slice equality. Dumps both on mismatch for quick diffing. */
#define TEST_ASSERT_EQ_BYTES(expected, expected_len, actual, actual_len) do { \
	const uint8_t *_e = (const uint8_t *)(expected); \
	size_t _el = (expected_len); \
	const uint8_t *_a = (const uint8_t *)(actual); \
	size_t _al = (actual_len); \
	if (_el != _al) { \
		fprintf(stderr, "  FAIL %s:%d: length mismatch: expected %zu, got %zu\n", \
		        __FILE__, __LINE__, _el, _al); \
		test_dump_bytes("expected", _e, _el); \
		test_dump_bytes("actual  ", _a, _al); \
		return 1; \
	} \
	for (size_t _i = 0; _i < _el; _i++) { \
		if (_e[_i] != _a[_i]) { \
			fprintf(stderr, "  FAIL %s:%d: byte %zu: expected 0x%02x, got 0x%02x\n", \
			        __FILE__, __LINE__, _i, _e[_i], _a[_i]); \
			test_dump_bytes("expected", _e, _el); \
			test_dump_bytes("actual  ", _a, _al); \
			return 1; \
		} \
	} \
} while (0)

static inline void
test_dump_bytes(const char *label, const uint8_t *buf, size_t len)
{
	fprintf(stderr, "    %s (%zu):", label, len);
	for (size_t i = 0; i < len; i++) {
		if (i % 16 == 0) fprintf(stderr, "\n     ");
		fprintf(stderr, " %02x", buf[i]);
	}
	fprintf(stderr, "\n");
}

/* Slurp a binary file — used for golden_message etc. Returns
 * malloc'd buffer; caller frees. Aborts on error. */
static inline uint8_t *
test_load_binary(const char *filename, size_t *out_len)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/%s",
		INTEGRATION_TEST_BINARIES_DIR, filename);
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "cannot open %s\n", path);
		abort();
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *buf = malloc((size_t)sz);
	if (!buf) abort();
	if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) abort();
	fclose(f);
	*out_len = (size_t)sz;
	return buf;
}

/* Runtime-sized encode: ask the message for its size, malloc a buffer,
 * encode into it. Caller frees. Returns buffer and fills *out_len. */
#define TEST_ENCODE(type_prefix, msg_ptr, out_buf, out_len_var) do { \
	(out_len_var) = type_prefix##_size(msg_ptr); \
	(out_buf) = malloc((out_len_var) ? (out_len_var) : 1); \
	if ((out_buf) == NULL) abort(); \
	struct gremlin_writer _w; \
	gremlin_writer_init(&_w, (out_buf), (out_len_var)); \
	type_prefix##_encode((msg_ptr), &_w); \
	if (_w.offset != (out_len_var)) { \
		fprintf(stderr, "  INTERNAL: %s_size said %zu, _encode wrote %zu\n", \
		        #type_prefix, (out_len_var), _w.offset); \
		abort(); \
	} \
} while (0)

/* Construct a gremlin_bytes from a C string literal. */
#define GB_CSTR(s) ((struct gremlin_bytes){ \
	.data = (const uint8_t *)(s), \
	.len  = sizeof(s) - 1, \
})
#define GB_LEN(p, n) ((struct gremlin_bytes){ .data = (const uint8_t *)(p), .len = (n) })

#endif /* !INTEGRATION_TEST_FRAMEWORK_H */

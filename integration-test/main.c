#include "test_framework.h"

#include "gremlin.h"
#include "unittest.pb.h"
#include "unittest_import.pb.h"
#include "unittest_import_public.pb.h"
#include "map_test.pb.h"
#include "whatsapp.pb.h"

/*
 * Integration tests — port of gremlin.zig/integration-test/main.zig.
 * Each test name matches the Zig-side test name so cross-referencing
 * between the two projects stays trivial.
 */

/* Matches the `test "simple write"` expected bytes in main.zig:13. */
static const uint8_t SIMPLE_EXPECTED[] = {
	8, 101, 16, 102, 146, 1, 2, 8, 118, 232, 3, 0, 240, 3, 0, 248, 3, 0,
	128, 4, 0, 136, 4, 0, 144, 4, 0, 157, 4, 0, 0, 0, 0, 161, 4, 0, 0, 0,
	0, 0, 0, 0, 0, 173, 4, 0, 0, 0, 0, 177, 4, 0, 0, 0, 0, 0, 0, 0, 0, 189,
	4, 0, 0, 0, 0, 193, 4, 0, 0, 0, 0, 0, 0, 0, 0, 200, 4, 0, 210, 4, 0,
	218, 4, 0, 136, 5, 0, 144, 5, 0, 152, 5, 0, 162, 5, 0, 170, 5, 0
};

static int
test_simple_write(void)
{
	protobuf_unittest_TestAllTypes_NestedMessage nested = { .bb = 118 };
	protobuf_unittest_TestAllTypes msg = {
		.optional_int32 = 101,
		.optional_int64 = 102,
		.optional_nested_message = &nested,
	};

	uint8_t *buf;
	size_t buf_len;
	TEST_ENCODE(protobuf_unittest_TestAllTypes, &msg, buf, buf_len);

	TEST_ASSERT_EQ_BYTES(SIMPLE_EXPECTED, sizeof SIMPLE_EXPECTED, buf, buf_len);
	free(buf);
	return 0;
}

static int
test_simple_read(void)
{
	protobuf_unittest_TestAllTypes_reader r;
	enum gremlin_error err = protobuf_unittest_TestAllTypes_reader_init(
		&r, SIMPLE_EXPECTED, sizeof SIMPLE_EXPECTED);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);

	TEST_ASSERT_EQ_INT(101,
		protobuf_unittest_TestAllTypes_reader_get_optional_int32(&r));
	TEST_ASSERT_EQ_INT(102,
		protobuf_unittest_TestAllTypes_reader_get_optional_int64(&r));

	protobuf_unittest_TestAllTypes_NestedMessage_reader nested;
	err = protobuf_unittest_TestAllTypes_reader_get_optional_nested_message(&r, &nested);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
	TEST_ASSERT_EQ_INT(118,
		protobuf_unittest_TestAllTypes_NestedMessage_reader_get_bb(&nested));
	return 0;
}

/* Matches `test "map kv: empty"` expected bytes in main.zig:41. */
static const uint8_t MAP_KV_EMPTY_EXPECTED[] = { 42, 4, 8, 0, 18, 0 };

static int
test_map_kv_empty(void)
{
	map_test_TestMap_MessageValue value0 = {0};
	map_test_TestMap_int32_to_message_field_entry entry0 = {
		.key = 0,
		.value = &value0,
	};
	map_test_TestMap msg = {
		.int32_to_message_field = &entry0,
		.int32_to_message_field_count = 1,
	};

	uint8_t *buf;
	size_t buf_len;
	TEST_ENCODE(map_test_TestMap, &msg, buf, buf_len);

	TEST_ASSERT_EQ_BYTES(MAP_KV_EMPTY_EXPECTED, sizeof MAP_KV_EMPTY_EXPECTED,
	                     buf, buf_len);

	map_test_TestMap_reader reader;
	enum gremlin_error err = map_test_TestMap_reader_init(&reader, buf, buf_len);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);

	bool found = false;
	map_test_TestMap_reader_int32_to_message_field_iter it =
		map_test_TestMap_reader_int32_to_message_field_begin(&reader);
	while (it.count_remaining > 0) {
		int32_t k;
		map_test_TestMap_MessageValue_reader v;
		err = map_test_TestMap_reader_int32_to_message_field_next(&it, &k, &v);
		TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
		if (k == 0) {
			TEST_ASSERT_EQ_INT(0,
				map_test_TestMap_MessageValue_reader_get_value(&v));
			found = true;
		}
	}
	TEST_ASSERT(found);

	free(buf);
	return 0;
}

static int
test_map_kv_value(void)
{
	map_test_TestMap_MessageValue value2 = { .value = 32 };
	map_test_TestMap_int32_to_message_field_entry entry = {
		.key = 2,
		.value = &value2,
	};
	map_test_TestMap msg = {
		.int32_to_message_field = &entry,
		.int32_to_message_field_count = 1,
	};

	uint8_t *buf;
	size_t buf_len;
	TEST_ENCODE(map_test_TestMap, &msg, buf, buf_len);

	map_test_TestMap_reader reader;
	enum gremlin_error err = map_test_TestMap_reader_init(&reader, buf, buf_len);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);

	bool found = false;
	map_test_TestMap_reader_int32_to_message_field_iter it =
		map_test_TestMap_reader_int32_to_message_field_begin(&reader);
	while (it.count_remaining > 0) {
		int32_t k;
		map_test_TestMap_MessageValue_reader v;
		err = map_test_TestMap_reader_int32_to_message_field_next(&it, &k, &v);
		TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
		if (k == 2) {
			TEST_ASSERT_EQ_INT(32,
				map_test_TestMap_MessageValue_reader_get_value(&v));
			found = true;
		}
	}
	TEST_ASSERT(found);

	free(buf);
	return 0;
}

/* Matches `test "negative values"` in main.zig:112. */
static const uint8_t NEGATIVE_VALUES_EXPECTED[] = {
	8, 156, 255, 255, 255, 255, 255, 255, 255, 255, 1,
	16, 155, 255, 255, 255, 255, 255, 255, 255, 255, 1,
	40, 203, 1,
	48, 205, 1,
	77, 152, 255, 255, 255,
	81, 151, 255, 255, 255, 255, 255, 255, 255,
	93, 0, 0, 210, 194,
	97, 0, 0, 0, 0, 0, 128, 90, 192,
	250, 1, 20, 184, 254, 255, 255, 255, 255, 255, 255, 255, 1,
	             212, 253, 255, 255, 255, 255, 255, 255, 255, 1,
	130, 2, 20, 183, 254, 255, 255, 255, 255, 255, 255, 255, 1,
	            211, 253, 255, 255, 255, 255, 255, 255, 255, 1,
	154, 2, 4, 147, 3, 219, 4,
	162, 2, 4, 149, 3, 221, 4,
	186, 2, 8, 52, 255, 255, 255, 208, 254, 255, 255,
	194, 2, 16, 51, 255, 255, 255, 255, 255, 255, 255,
	             207, 254, 255, 255, 255, 255, 255, 255,
	202, 2, 8, 0, 0, 77, 195, 0, 128, 152, 195,
	210, 2, 16, 0, 0, 0, 0, 0, 192, 105, 192, 0, 0, 0, 0, 0, 32, 115, 192,
	232, 3, 0, 240, 3, 0, 248, 3, 0, 128, 4, 0, 136, 4, 0, 144, 4, 0,
	157, 4, 0, 0, 0, 0,
	161, 4, 0, 0, 0, 0, 0, 0, 0, 0,
	173, 4, 0, 0, 0, 0,
	177, 4, 0, 0, 0, 0, 0, 0, 0, 0,
	189, 4, 0, 0, 0, 0,
	193, 4, 0, 0, 0, 0, 0, 0, 0, 0,
	200, 4, 0, 210, 4, 0, 218, 4, 0,
	136, 5, 0, 144, 5, 0, 152, 5, 0, 162, 5, 0, 170, 5, 0
};

static int
test_negative_values(void)
{
	int32_t rep_int32[]    = { -200, -300 };
	int64_t rep_int64[]    = { -201, -301 };
	int32_t rep_sint32[]   = { -202, -302 };
	int64_t rep_sint64[]   = { -203, -303 };
	int32_t rep_sfixed32[] = { -204, -304 };
	int64_t rep_sfixed64[] = { -205, -305 };
	float   rep_float[]    = { -205, -305 };
	double  rep_double[]   = { -206, -306 };

	protobuf_unittest_TestAllTypes msg = {
		.optional_int32    = -100,
		.optional_int64    = -101,
		.optional_sint32   = -102,
		.optional_sint64   = -103,
		.optional_sfixed32 = -104,
		.optional_sfixed64 = -105,
		.optional_float    = -105,
		.optional_double   = -106,

		.repeated_int32    = rep_int32,    .repeated_int32_count    = 2,
		.repeated_int64    = rep_int64,    .repeated_int64_count    = 2,
		.repeated_sint32   = rep_sint32,   .repeated_sint32_count   = 2,
		.repeated_sint64   = rep_sint64,   .repeated_sint64_count   = 2,
		.repeated_sfixed32 = rep_sfixed32, .repeated_sfixed32_count = 2,
		.repeated_sfixed64 = rep_sfixed64, .repeated_sfixed64_count = 2,
		.repeated_float    = rep_float,    .repeated_float_count    = 2,
		.repeated_double   = rep_double,   .repeated_double_count   = 2,
	};

	uint8_t *buf;
	size_t buf_len;
	TEST_ENCODE(protobuf_unittest_TestAllTypes, &msg, buf, buf_len);

	TEST_ASSERT_EQ_BYTES(NEGATIVE_VALUES_EXPECTED,
	                     sizeof NEGATIVE_VALUES_EXPECTED,
	                     buf, buf_len);

	/* Round-trip: parse and verify. */
	protobuf_unittest_TestAllTypes_reader r;
	enum gremlin_error err = protobuf_unittest_TestAllTypes_reader_init(
		&r, buf, buf_len);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);

	TEST_ASSERT_EQ_INT(-100, protobuf_unittest_TestAllTypes_reader_get_optional_int32(&r));
	TEST_ASSERT_EQ_INT(-101, protobuf_unittest_TestAllTypes_reader_get_optional_int64(&r));
	TEST_ASSERT_EQ_INT(-102, protobuf_unittest_TestAllTypes_reader_get_optional_sint32(&r));
	TEST_ASSERT_EQ_INT(-103, protobuf_unittest_TestAllTypes_reader_get_optional_sint64(&r));
	TEST_ASSERT_EQ_INT(-104, protobuf_unittest_TestAllTypes_reader_get_optional_sfixed32(&r));
	TEST_ASSERT_EQ_INT(-105, protobuf_unittest_TestAllTypes_reader_get_optional_sfixed64(&r));
	TEST_ASSERT_EQ_FLOAT(-105.0f, protobuf_unittest_TestAllTypes_reader_get_optional_float(&r));
	TEST_ASSERT_EQ_FLOAT(-106.0, protobuf_unittest_TestAllTypes_reader_get_optional_double(&r));

	/* Verify repeated fields via iterators. */
#define CHECK_REPEATED(kind, ctype, expected0, expected1) do { \
	protobuf_unittest_TestAllTypes_reader_repeated_##kind##_iter _it = \
		protobuf_unittest_TestAllTypes_reader_repeated_##kind##_begin(&r); \
	ctype _v; \
	size_t _n = 0; \
	while (_it.count_remaining > 0) { \
		err = protobuf_unittest_TestAllTypes_reader_repeated_##kind##_next(&_it, &_v); \
		TEST_ASSERT_EQ_INT(GREMLIN_OK, err); \
		if (_n == 0) TEST_ASSERT_EQ_FLOAT((double)(expected0), (double)_v); \
		else if (_n == 1) TEST_ASSERT_EQ_FLOAT((double)(expected1), (double)_v); \
		_n++; \
	} \
	TEST_ASSERT_EQ_UINT(2, _n); \
} while (0)

	CHECK_REPEATED(int32,    int32_t, -200, -300);
	CHECK_REPEATED(int64,    int64_t, -201, -301);
	CHECK_REPEATED(sint32,   int32_t, -202, -302);
	CHECK_REPEATED(sint64,   int64_t, -203, -303);
	CHECK_REPEATED(sfixed32, int32_t, -204, -304);
	CHECK_REPEATED(sfixed64, int64_t, -205, -305);
	CHECK_REPEATED(float,    float,   -205, -305);
	CHECK_REPEATED(double,   double,  -206, -306);
#undef CHECK_REPEATED

	free(buf);
	return 0;
}

/* `test "complex read"` + `test "complex write"` both reference the
 * same expected-bytes blob — a TestAllTypes with every field set.
 * Main.zig:268 / :338. */
static const uint8_t COMPLEX_EXPECTED[] = {
	8, 101, 16, 102, 24, 103, 32, 104, 40, 210, 1, 48, 212, 1,
	61, 107, 0, 0, 0, 65, 108, 0, 0, 0, 0, 0, 0, 0,
	77, 109, 0, 0, 0, 81, 110, 0, 0, 0, 0, 0, 0, 0,
	93, 0, 0, 222, 66, 97, 0, 0, 0, 0, 0, 0, 92, 64,
	104, 1, 114, 3, 49, 49, 53, 122, 3, 49, 49, 54,
	146, 1, 2, 8, 118, 154, 1, 2, 8, 119, 162, 1, 2, 8, 120,
	168, 1, 3, 176, 1, 6, 184, 1, 9, 194, 1, 3, 49, 50, 52,
	202, 1, 3, 49, 50, 53, 210, 1, 2, 8, 126, 218, 1, 2, 8, 127,
	226, 1, 3, 8, 128, 1,
	250, 1, 4, 201, 1, 173, 2,
	130, 2, 4, 202, 1, 174, 2,
	138, 2, 4, 203, 1, 175, 2,
	146, 2, 4, 204, 1, 176, 2,
	154, 2, 4, 154, 3, 226, 4,
	162, 2, 4, 156, 3, 228, 4,
	170, 2, 8, 207, 0, 0, 0, 51, 1, 0, 0,
	178, 2, 16, 208, 0, 0, 0, 0, 0, 0, 0, 52, 1, 0, 0, 0, 0, 0, 0,
	186, 2, 8, 209, 0, 0, 0, 53, 1, 0, 0,
	194, 2, 16, 210, 0, 0, 0, 0, 0, 0, 0, 54, 1, 0, 0, 0, 0, 0, 0,
	202, 2, 8, 0, 0, 83, 67, 0, 128, 155, 67,
	210, 2, 16, 0, 0, 0, 0, 0, 128, 106, 64, 0, 0, 0, 0, 0, 128, 115, 64,
	218, 2, 2, 1, 0,
	226, 2, 3, 50, 49, 53, 226, 2, 3, 51, 49, 53,
	234, 2, 3, 50, 49, 54, 234, 2, 3, 51, 49, 54,
	130, 3, 3, 8, 218, 1, 130, 3, 3, 8, 190, 2,
	138, 3, 3, 8, 219, 1, 138, 3, 3, 8, 191, 2,
	146, 3, 3, 8, 220, 1, 146, 3, 3, 8, 192, 2,
	154, 3, 2, 2, 3, 162, 3, 2, 5, 6, 170, 3, 2, 8, 9,
	178, 3, 3, 50, 50, 52, 178, 3, 3, 51, 50, 52,
	186, 3, 3, 50, 50, 53, 186, 3, 3, 51, 50, 53,
	202, 3, 3, 8, 227, 1, 202, 3, 3, 8, 199, 2,
	232, 3, 145, 3, 240, 3, 146, 3, 248, 3, 147, 3,
	128, 4, 148, 3, 136, 4, 170, 6, 144, 4, 172, 6,
	157, 4, 151, 1, 0, 0, 161, 4, 152, 1, 0, 0, 0, 0, 0, 0,
	173, 4, 153, 1, 0, 0, 177, 4, 154, 1, 0, 0, 0, 0, 0, 0,
	189, 4, 0, 128, 205, 67, 193, 4, 0, 0, 0, 0, 0, 192, 121, 64,
	200, 4, 0,
	210, 4, 3, 52, 49, 53, 218, 4, 3, 52, 49, 54,
	136, 5, 1, 144, 5, 4, 152, 5, 7,
	162, 5, 3, 52, 50, 52, 170, 5, 3, 52, 50, 53,
	248, 6, 217, 4
};

static int
test_complex_write(void)
{
	protobuf_unittest_TestAllTypes_NestedMessage n118 = { .bb = 118 };
	protobuf_unittest_ForeignMessage f119 = { .c = 119 };
	protobuf_unittest_import_ImportMessage i120 = { .d = 120 };
	protobuf_unittest_import_PublicImportMessage p126 = { .e = 126 };
	protobuf_unittest_TestAllTypes_NestedMessage lazy127 = { .bb = 127 };
	protobuf_unittest_TestAllTypes_NestedMessage ulazy128 = { .bb = 128 };

	int32_t rep_int32[]    = { 201, 301 };
	int64_t rep_int64[]    = { 202, 302 };
	uint32_t rep_uint32[]  = { 203, 303 };
	uint64_t rep_uint64[]  = { 204, 304 };
	int32_t rep_sint32[]   = { 205, 305 };
	int64_t rep_sint64[]   = { 206, 306 };
	uint32_t rep_fixed32[] = { 207, 307 };
	uint64_t rep_fixed64[] = { 208, 308 };
	int32_t rep_sfixed32[] = { 209, 309 };
	int64_t rep_sfixed64[] = { 210, 310 };
	float    rep_float[]   = { 211, 311 };
	double   rep_double[]  = { 212, 312 };
	bool     rep_bool[]    = { true, false };
	struct gremlin_bytes rep_string[] = { GB_CSTR("215"), GB_CSTR("315") };
	struct gremlin_bytes rep_bytes[]  = { GB_CSTR("216"), GB_CSTR("316") };
	struct gremlin_bytes rep_sp[]     = { GB_CSTR("224"), GB_CSTR("324") };
	struct gremlin_bytes rep_cord[]   = { GB_CSTR("225"), GB_CSTR("325") };

	protobuf_unittest_TestAllTypes_NestedMessage n218 = { .bb = 218 };
	protobuf_unittest_TestAllTypes_NestedMessage n318 = { .bb = 318 };
	const protobuf_unittest_TestAllTypes_NestedMessage *rep_nested[] = { &n218, &n318 };

	protobuf_unittest_ForeignMessage f219 = { .c = 219 };
	protobuf_unittest_ForeignMessage f319 = { .c = 319 };
	const protobuf_unittest_ForeignMessage *rep_foreign[] = { &f219, &f319 };

	protobuf_unittest_import_ImportMessage i220 = { .d = 220 };
	protobuf_unittest_import_ImportMessage i320 = { .d = 320 };
	const protobuf_unittest_import_ImportMessage *rep_import[] = { &i220, &i320 };

	protobuf_unittest_TestAllTypes_NestedMessage nl227 = { .bb = 227 };
	protobuf_unittest_TestAllTypes_NestedMessage nl327 = { .bb = 327 };
	const protobuf_unittest_TestAllTypes_NestedMessage *rep_lazy[] = { &nl227, &nl327 };

	protobuf_unittest_TestAllTypes_NestedEnum rep_nested_enum[] = {
		protobuf_unittest_TestAllTypes_NestedEnum_BAR,
		protobuf_unittest_TestAllTypes_NestedEnum_BAZ,
	};
	protobuf_unittest_ForeignEnum rep_foreign_enum[] = {
		protobuf_unittest_ForeignEnum_FOREIGN_BAR,
		protobuf_unittest_ForeignEnum_FOREIGN_BAZ,
	};
	protobuf_unittest_import_ImportEnum rep_import_enum[] = {
		protobuf_unittest_import_ImportEnum_IMPORT_BAR,
		protobuf_unittest_import_ImportEnum_IMPORT_BAZ,
	};

	protobuf_unittest_TestAllTypes msg = {
		.optional_int32    = 101,
		.optional_int64    = 102,
		.optional_uint32   = 103,
		.optional_uint64   = 104,
		.optional_sint32   = 105,
		.optional_sint64   = 106,
		.optional_fixed32  = 107,
		.optional_fixed64  = 108,
		.optional_sfixed32 = 109,
		.optional_sfixed64 = 110,
		.optional_float    = 111,
		.optional_double   = 112,
		.optional_bool     = true,
		.optional_string   = GB_CSTR("115"),
		.optional_bytes    = GB_CSTR("116"),
		.optional_nested_message          = &n118,
		.optional_foreign_message         = &f119,
		.optional_import_message          = &i120,
		.optional_public_import_message   = &p126,
		.optional_lazy_message            = &lazy127,
		.optional_unverified_lazy_message = &ulazy128,
		.optional_nested_enum  = protobuf_unittest_TestAllTypes_NestedEnum_BAZ,
		.optional_foreign_enum = protobuf_unittest_ForeignEnum_FOREIGN_BAZ,
		.optional_import_enum  = protobuf_unittest_import_ImportEnum_IMPORT_BAZ,
		.optional_string_piece = GB_CSTR("124"),
		.optional_cord         = GB_CSTR("125"),

		.repeated_int32    = rep_int32,    .repeated_int32_count    = 2,
		.repeated_int64    = rep_int64,    .repeated_int64_count    = 2,
		.repeated_uint32   = rep_uint32,   .repeated_uint32_count   = 2,
		.repeated_uint64   = rep_uint64,   .repeated_uint64_count   = 2,
		.repeated_sint32   = rep_sint32,   .repeated_sint32_count   = 2,
		.repeated_sint64   = rep_sint64,   .repeated_sint64_count   = 2,
		.repeated_fixed32  = rep_fixed32,  .repeated_fixed32_count  = 2,
		.repeated_fixed64  = rep_fixed64,  .repeated_fixed64_count  = 2,
		.repeated_sfixed32 = rep_sfixed32, .repeated_sfixed32_count = 2,
		.repeated_sfixed64 = rep_sfixed64, .repeated_sfixed64_count = 2,
		.repeated_float    = rep_float,    .repeated_float_count    = 2,
		.repeated_double   = rep_double,   .repeated_double_count   = 2,
		.repeated_bool     = rep_bool,     .repeated_bool_count     = 2,
		.repeated_string       = rep_string, .repeated_string_count       = 2,
		.repeated_bytes        = rep_bytes,  .repeated_bytes_count        = 2,
		.repeated_string_piece = rep_sp,     .repeated_string_piece_count = 2,
		.repeated_cord         = rep_cord,   .repeated_cord_count         = 2,
		.repeated_nested_message  = rep_nested,  .repeated_nested_message_count  = 2,
		.repeated_foreign_message = rep_foreign, .repeated_foreign_message_count = 2,
		.repeated_import_message  = rep_import,  .repeated_import_message_count  = 2,
		.repeated_lazy_message    = rep_lazy,    .repeated_lazy_message_count    = 2,
		.repeated_nested_enum  = rep_nested_enum,  .repeated_nested_enum_count  = 2,
		.repeated_foreign_enum = rep_foreign_enum, .repeated_foreign_enum_count = 2,
		.repeated_import_enum  = rep_import_enum,  .repeated_import_enum_count  = 2,

		.default_int32    = 401,
		.default_int64    = 402,
		.default_uint32   = 403,
		.default_uint64   = 404,
		.default_sint32   = 405,
		.default_sint64   = 406,
		.default_fixed32  = 407,
		.default_fixed64  = 408,
		.default_sfixed32 = 409,
		.default_sfixed64 = 410,
		.default_float    = 411,
		.default_double   = 412,
		.default_bool     = false,
		.default_string   = GB_CSTR("415"),
		.default_bytes    = GB_CSTR("416"),
		.default_nested_enum  = protobuf_unittest_TestAllTypes_NestedEnum_FOO,
		.default_foreign_enum = protobuf_unittest_ForeignEnum_FOREIGN_FOO,
		.default_import_enum  = protobuf_unittest_import_ImportEnum_IMPORT_FOO,
		.default_string_piece = GB_CSTR("424"),
		.default_cord         = GB_CSTR("425"),

		.oneof_uint32 = 601,
	};

	uint8_t *buf;
	size_t buf_len;
	TEST_ENCODE(protobuf_unittest_TestAllTypes, &msg, buf, buf_len);

	TEST_ASSERT_EQ_BYTES(COMPLEX_EXPECTED, sizeof COMPLEX_EXPECTED,
	                     buf, buf_len);
	free(buf);
	return 0;
}

static int
test_complex_read(void)
{
	protobuf_unittest_TestAllTypes_reader r;
	enum gremlin_error err = protobuf_unittest_TestAllTypes_reader_init(
		&r, COMPLEX_EXPECTED, sizeof COMPLEX_EXPECTED);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);

	TEST_ASSERT_EQ_INT(101, protobuf_unittest_TestAllTypes_reader_get_optional_int32(&r));
	TEST_ASSERT_EQ_INT(102, protobuf_unittest_TestAllTypes_reader_get_optional_int64(&r));
	TEST_ASSERT_EQ_UINT(103, protobuf_unittest_TestAllTypes_reader_get_optional_uint32(&r));
	TEST_ASSERT_EQ_UINT(104, protobuf_unittest_TestAllTypes_reader_get_optional_uint64(&r));
	TEST_ASSERT_EQ_INT(105, protobuf_unittest_TestAllTypes_reader_get_optional_sint32(&r));
	TEST_ASSERT_EQ_INT(106, protobuf_unittest_TestAllTypes_reader_get_optional_sint64(&r));
	TEST_ASSERT_EQ_UINT(107, protobuf_unittest_TestAllTypes_reader_get_optional_fixed32(&r));
	TEST_ASSERT_EQ_UINT(108, protobuf_unittest_TestAllTypes_reader_get_optional_fixed64(&r));
	TEST_ASSERT_EQ_INT(109, protobuf_unittest_TestAllTypes_reader_get_optional_sfixed32(&r));
	TEST_ASSERT_EQ_INT(110, protobuf_unittest_TestAllTypes_reader_get_optional_sfixed64(&r));
	TEST_ASSERT_EQ_FLOAT(111.0f, protobuf_unittest_TestAllTypes_reader_get_optional_float(&r));
	TEST_ASSERT_EQ_FLOAT(112.0, protobuf_unittest_TestAllTypes_reader_get_optional_double(&r));
	TEST_ASSERT_EQ_BOOL(true, protobuf_unittest_TestAllTypes_reader_get_optional_bool(&r));
	TEST_ASSERT_EQ_STR("115", protobuf_unittest_TestAllTypes_reader_get_optional_string(&r));
	TEST_ASSERT_EQ_STR("116", protobuf_unittest_TestAllTypes_reader_get_optional_bytes(&r));

	protobuf_unittest_TestAllTypes_NestedMessage_reader nested;
	err = protobuf_unittest_TestAllTypes_reader_get_optional_nested_message(&r, &nested);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
	TEST_ASSERT_EQ_INT(118,
		protobuf_unittest_TestAllTypes_NestedMessage_reader_get_bb(&nested));

	protobuf_unittest_ForeignMessage_reader foreign;
	err = protobuf_unittest_TestAllTypes_reader_get_optional_foreign_message(&r, &foreign);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
	TEST_ASSERT_EQ_INT(119,
		protobuf_unittest_ForeignMessage_reader_get_c(&foreign));

	protobuf_unittest_import_ImportMessage_reader imp;
	err = protobuf_unittest_TestAllTypes_reader_get_optional_import_message(&r, &imp);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
	TEST_ASSERT_EQ_INT(120,
		protobuf_unittest_import_ImportMessage_reader_get_d(&imp));

	/* repeated_int32 spot-check. */
	protobuf_unittest_TestAllTypes_reader_repeated_int32_iter it =
		protobuf_unittest_TestAllTypes_reader_repeated_int32_begin(&r);
	int32_t v;
	size_t n = 0;
	while (it.count_remaining > 0) {
		err = protobuf_unittest_TestAllTypes_reader_repeated_int32_next(&it, &v);
		TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
		if (n == 0) TEST_ASSERT_EQ_INT(201, v);
		else if (n == 1) TEST_ASSERT_EQ_INT(301, v);
		n++;
	}
	TEST_ASSERT_EQ_UINT(2, n);

	TEST_ASSERT_EQ_INT(protobuf_unittest_TestAllTypes_NestedEnum_BAZ,
		protobuf_unittest_TestAllTypes_reader_get_optional_nested_enum(&r));
	TEST_ASSERT_EQ_INT(protobuf_unittest_ForeignEnum_FOREIGN_BAZ,
		protobuf_unittest_TestAllTypes_reader_get_optional_foreign_enum(&r));
	TEST_ASSERT_EQ_INT(protobuf_unittest_import_ImportEnum_IMPORT_BAZ,
		protobuf_unittest_TestAllTypes_reader_get_optional_import_enum(&r));

	TEST_ASSERT_EQ_STR("124", protobuf_unittest_TestAllTypes_reader_get_optional_string_piece(&r));
	TEST_ASSERT_EQ_STR("125", protobuf_unittest_TestAllTypes_reader_get_optional_cord(&r));

	/* Defaults — the getter returns the proto2 [default] if not set, and
	 * the explicit value if set. The wire has explicit 401..404 etc. */
	TEST_ASSERT_EQ_INT(401, protobuf_unittest_TestAllTypes_reader_get_default_int32(&r));
	TEST_ASSERT_EQ_INT(402, protobuf_unittest_TestAllTypes_reader_get_default_int64(&r));
	TEST_ASSERT_EQ_UINT(403, protobuf_unittest_TestAllTypes_reader_get_default_uint32(&r));
	TEST_ASSERT_EQ_UINT(404, protobuf_unittest_TestAllTypes_reader_get_default_uint64(&r));
	TEST_ASSERT_EQ_BOOL(false, protobuf_unittest_TestAllTypes_reader_get_default_bool(&r));
	TEST_ASSERT_EQ_STR("415", protobuf_unittest_TestAllTypes_reader_get_default_string(&r));
	TEST_ASSERT_EQ_STR("416", protobuf_unittest_TestAllTypes_reader_get_default_bytes(&r));

	TEST_ASSERT_EQ_UINT(601,
		protobuf_unittest_TestAllTypes_reader_get_oneof_uint32(&r));
	return 0;
}

/* `test "map parsing"` — decode binaries/map_test and verify each of
 * the 8 map variants resolves its entries. Main.zig:487. */
static int
test_map_parsing(void)
{
	size_t data_len;
	uint8_t *data = test_load_binary("map_test", &data_len);

	map_test_TestMap_reader r;
	enum gremlin_error err;

	/* int32 → int32 */
	err = map_test_TestMap_reader_init(&r, data, data_len);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
	{
		int32_t v100 = -1, v200 = -1;
		map_test_TestMap_reader_int32_to_int32_field_iter it =
			map_test_TestMap_reader_int32_to_int32_field_begin(&r);
		int32_t k, v;
		while (it.count_remaining > 0) {
			err = map_test_TestMap_reader_int32_to_int32_field_next(&it, &k, &v);
			TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
			if (k == 100) v100 = v;
			else if (k == 200) v200 = v;
		}
		TEST_ASSERT_EQ_INT(101, v100);
		TEST_ASSERT_EQ_INT(201, v200);
	}

	/* int32 → string */
	err = map_test_TestMap_reader_init(&r, data, data_len);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
	{
		bool found101 = false, found201 = false;
		map_test_TestMap_reader_int32_to_string_field_iter it =
			map_test_TestMap_reader_int32_to_string_field_begin(&r);
		int32_t k; struct gremlin_bytes v;
		while (it.count_remaining > 0) {
			err = map_test_TestMap_reader_int32_to_string_field_next(&it, &k, &v);
			TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
			if (k == 101) { TEST_ASSERT_EQ_STR("101", v); found101 = true; }
			else if (k == 201) { TEST_ASSERT_EQ_STR("201", v); found201 = true; }
		}
		TEST_ASSERT(found101); TEST_ASSERT(found201);
	}

	/* int32 → bytes */
	err = map_test_TestMap_reader_init(&r, data, data_len);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
	{
		bool found102 = false, found202 = false;
		map_test_TestMap_reader_int32_to_bytes_field_iter it =
			map_test_TestMap_reader_int32_to_bytes_field_begin(&r);
		int32_t k; struct gremlin_bytes v;
		while (it.count_remaining > 0) {
			err = map_test_TestMap_reader_int32_to_bytes_field_next(&it, &k, &v);
			TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
			if (k == 102) {
				TEST_ASSERT_EQ_UINT(1, v.len);
				TEST_ASSERT_EQ_INT(102, v.data[0]);
				found102 = true;
			} else if (k == 202) {
				TEST_ASSERT_EQ_UINT(1, v.len);
				TEST_ASSERT_EQ_INT(202, v.data[0]);
				found202 = true;
			}
		}
		TEST_ASSERT(found102); TEST_ASSERT(found202);
	}

	/* int32 → enum */
	err = map_test_TestMap_reader_init(&r, data, data_len);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
	{
		map_test_TestMap_EnumValue v103 = map_test_TestMap_EnumValue_FOO,
		                            v203 = map_test_TestMap_EnumValue_FOO;
		bool found103 = false, found203 = false;
		map_test_TestMap_reader_int32_to_enum_field_iter it =
			map_test_TestMap_reader_int32_to_enum_field_begin(&r);
		int32_t k; map_test_TestMap_EnumValue v;
		while (it.count_remaining > 0) {
			err = map_test_TestMap_reader_int32_to_enum_field_next(&it, &k, &v);
			TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
			if (k == 103) { v103 = v; found103 = true; }
			else if (k == 203) { v203 = v; found203 = true; }
		}
		TEST_ASSERT(found103); TEST_ASSERT(found203);
		TEST_ASSERT_EQ_INT(map_test_TestMap_EnumValue_FOO, v103);
		TEST_ASSERT_EQ_INT(map_test_TestMap_EnumValue_BAR, v203);
	}

	/* string → int32 */
	err = map_test_TestMap_reader_init(&r, data, data_len);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
	{
		bool f105 = false, f205 = false;
		map_test_TestMap_reader_string_to_int32_field_iter it =
			map_test_TestMap_reader_string_to_int32_field_begin(&r);
		struct gremlin_bytes k; int32_t v;
		while (it.count_remaining > 0) {
			err = map_test_TestMap_reader_string_to_int32_field_next(&it, &k, &v);
			TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
			if (k.len == 3 && memcmp(k.data, "105", 3) == 0) {
				TEST_ASSERT_EQ_INT(105, v); f105 = true;
			} else if (k.len == 3 && memcmp(k.data, "205", 3) == 0) {
				TEST_ASSERT_EQ_INT(205, v); f205 = true;
			}
		}
		TEST_ASSERT(f105); TEST_ASSERT(f205);
	}

	/* uint32 → int32 */
	err = map_test_TestMap_reader_init(&r, data, data_len);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
	{
		bool f106 = false, f206 = false;
		map_test_TestMap_reader_uint32_to_int32_field_iter it =
			map_test_TestMap_reader_uint32_to_int32_field_begin(&r);
		uint32_t k; int32_t v;
		while (it.count_remaining > 0) {
			err = map_test_TestMap_reader_uint32_to_int32_field_next(&it, &k, &v);
			TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
			if (k == 106) { TEST_ASSERT_EQ_INT(106, v); f106 = true; }
			else if (k == 206) { TEST_ASSERT_EQ_INT(206, v); f206 = true; }
		}
		TEST_ASSERT(f106); TEST_ASSERT(f206);
	}

	/* int64 → int32 */
	err = map_test_TestMap_reader_init(&r, data, data_len);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
	{
		bool f107 = false, f207 = false;
		map_test_TestMap_reader_int64_to_int32_field_iter it =
			map_test_TestMap_reader_int64_to_int32_field_begin(&r);
		int64_t k; int32_t v;
		while (it.count_remaining > 0) {
			err = map_test_TestMap_reader_int64_to_int32_field_next(&it, &k, &v);
			TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
			if (k == 107) { TEST_ASSERT_EQ_INT(107, v); f107 = true; }
			else if (k == 207) { TEST_ASSERT_EQ_INT(207, v); f207 = true; }
		}
		TEST_ASSERT(f107); TEST_ASSERT(f207);
	}

	/* int32 → message */
	err = map_test_TestMap_reader_init(&r, data, data_len);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
	{
		bool f104 = false, f204 = false;
		map_test_TestMap_reader_int32_to_message_field_iter it =
			map_test_TestMap_reader_int32_to_message_field_begin(&r);
		int32_t k; map_test_TestMap_MessageValue_reader v;
		while (it.count_remaining > 0) {
			err = map_test_TestMap_reader_int32_to_message_field_next(&it, &k, &v);
			TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
			if (k == 104) {
				TEST_ASSERT_EQ_INT(104,
					map_test_TestMap_MessageValue_reader_get_value(&v));
				f104 = true;
			} else if (k == 204) {
				TEST_ASSERT_EQ_INT(204,
					map_test_TestMap_MessageValue_reader_get_value(&v));
				f204 = true;
			}
		}
		TEST_ASSERT(f104); TEST_ASSERT(f204);
	}

	free(data);
	return 0;
}

/* `test "golden message"` — parse the reference golden_message binary
 * produced by Google's protobuf; validates reader correctness across
 * every major field kind. Main.zig:609. */
static int
test_golden_message(void)
{
	size_t data_len;
	uint8_t *data = test_load_binary("golden_message", &data_len);

	protobuf_unittest_TestAllTypes_reader r;
	enum gremlin_error err = protobuf_unittest_TestAllTypes_reader_init(
		&r, data, data_len);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);

	TEST_ASSERT_EQ_INT(101, protobuf_unittest_TestAllTypes_reader_get_optional_int32(&r));
	TEST_ASSERT_EQ_INT(102, protobuf_unittest_TestAllTypes_reader_get_optional_int64(&r));
	TEST_ASSERT_EQ_UINT(103, protobuf_unittest_TestAllTypes_reader_get_optional_uint32(&r));
	TEST_ASSERT_EQ_UINT(104, protobuf_unittest_TestAllTypes_reader_get_optional_uint64(&r));
	TEST_ASSERT_EQ_INT(105, protobuf_unittest_TestAllTypes_reader_get_optional_sint32(&r));
	TEST_ASSERT_EQ_INT(106, protobuf_unittest_TestAllTypes_reader_get_optional_sint64(&r));
	TEST_ASSERT_EQ_UINT(107, protobuf_unittest_TestAllTypes_reader_get_optional_fixed32(&r));
	TEST_ASSERT_EQ_UINT(108, protobuf_unittest_TestAllTypes_reader_get_optional_fixed64(&r));
	TEST_ASSERT_EQ_INT(109, protobuf_unittest_TestAllTypes_reader_get_optional_sfixed32(&r));
	TEST_ASSERT_EQ_INT(110, protobuf_unittest_TestAllTypes_reader_get_optional_sfixed64(&r));
	TEST_ASSERT_EQ_FLOAT(111.0f, protobuf_unittest_TestAllTypes_reader_get_optional_float(&r));
	TEST_ASSERT_EQ_FLOAT(112.0, protobuf_unittest_TestAllTypes_reader_get_optional_double(&r));
	TEST_ASSERT_EQ_BOOL(true, protobuf_unittest_TestAllTypes_reader_get_optional_bool(&r));
	TEST_ASSERT_EQ_STR("115", protobuf_unittest_TestAllTypes_reader_get_optional_string(&r));
	TEST_ASSERT_EQ_STR("116", protobuf_unittest_TestAllTypes_reader_get_optional_bytes(&r));

	{
		protobuf_unittest_TestAllTypes_NestedMessage_reader sub;
		err = protobuf_unittest_TestAllTypes_reader_get_optional_nested_message(&r, &sub);
		TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
		TEST_ASSERT_EQ_INT(118,
			protobuf_unittest_TestAllTypes_NestedMessage_reader_get_bb(&sub));
	}
	{
		protobuf_unittest_ForeignMessage_reader sub;
		err = protobuf_unittest_TestAllTypes_reader_get_optional_foreign_message(&r, &sub);
		TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
		TEST_ASSERT_EQ_INT(119,
			protobuf_unittest_ForeignMessage_reader_get_c(&sub));
	}
	{
		protobuf_unittest_import_ImportMessage_reader sub;
		err = protobuf_unittest_TestAllTypes_reader_get_optional_import_message(&r, &sub);
		TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
		TEST_ASSERT_EQ_INT(120,
			protobuf_unittest_import_ImportMessage_reader_get_d(&sub));
	}
	{
		protobuf_unittest_import_PublicImportMessage_reader sub;
		err = protobuf_unittest_TestAllTypes_reader_get_optional_public_import_message(&r, &sub);
		TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
		TEST_ASSERT_EQ_INT(126,
			protobuf_unittest_import_PublicImportMessage_reader_get_e(&sub));
	}
	{
		protobuf_unittest_TestAllTypes_NestedMessage_reader sub;
		err = protobuf_unittest_TestAllTypes_reader_get_optional_lazy_message(&r, &sub);
		TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
		TEST_ASSERT_EQ_INT(127,
			protobuf_unittest_TestAllTypes_NestedMessage_reader_get_bb(&sub));
	}
	{
		protobuf_unittest_TestAllTypes_NestedMessage_reader sub;
		err = protobuf_unittest_TestAllTypes_reader_get_optional_unverified_lazy_message(&r, &sub);
		TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
		TEST_ASSERT_EQ_INT(128,
			protobuf_unittest_TestAllTypes_NestedMessage_reader_get_bb(&sub));
	}

	TEST_ASSERT_EQ_INT(protobuf_unittest_TestAllTypes_NestedEnum_BAZ,
		protobuf_unittest_TestAllTypes_reader_get_optional_nested_enum(&r));
	TEST_ASSERT_EQ_INT(protobuf_unittest_ForeignEnum_FOREIGN_BAZ,
		protobuf_unittest_TestAllTypes_reader_get_optional_foreign_enum(&r));
	TEST_ASSERT_EQ_INT(protobuf_unittest_import_ImportEnum_IMPORT_BAZ,
		protobuf_unittest_TestAllTypes_reader_get_optional_import_enum(&r));

	TEST_ASSERT_EQ_STR("124", protobuf_unittest_TestAllTypes_reader_get_optional_string_piece(&r));
	TEST_ASSERT_EQ_STR("125", protobuf_unittest_TestAllTypes_reader_get_optional_cord(&r));

	/* repeated_int32 check. */
	{
		protobuf_unittest_TestAllTypes_reader_repeated_int32_iter it =
			protobuf_unittest_TestAllTypes_reader_repeated_int32_begin(&r);
		int32_t v; size_t n = 0;
		while (it.count_remaining > 0) {
			err = protobuf_unittest_TestAllTypes_reader_repeated_int32_next(&it, &v);
			TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
			if (n == 0) TEST_ASSERT_EQ_INT(201, v);
			else if (n == 1) TEST_ASSERT_EQ_INT(301, v);
			n++;
		}
		TEST_ASSERT_EQ_UINT(2, n);
	}
	{
		protobuf_unittest_TestAllTypes_reader_repeated_int64_iter it =
			protobuf_unittest_TestAllTypes_reader_repeated_int64_begin(&r);
		int64_t v; size_t n = 0;
		while (it.count_remaining > 0) {
			err = protobuf_unittest_TestAllTypes_reader_repeated_int64_next(&it, &v);
			TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
			if (n == 0) TEST_ASSERT_EQ_INT(202, v);
			else if (n == 1) TEST_ASSERT_EQ_INT(302, v);
			n++;
		}
		TEST_ASSERT_EQ_UINT(2, n);
	}

	/* oneof fields — the getter returns the last-wire-seen for that
	 * oneof group; all four oneof variants present on the wire sets
	 * each of their has-bits. */
	TEST_ASSERT_EQ_UINT(601, protobuf_unittest_TestAllTypes_reader_get_oneof_uint32(&r));
	{
		protobuf_unittest_TestAllTypes_NestedMessage_reader sub;
		err = protobuf_unittest_TestAllTypes_reader_get_oneof_nested_message(&r, &sub);
		TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
		TEST_ASSERT_EQ_INT(602,
			protobuf_unittest_TestAllTypes_NestedMessage_reader_get_bb(&sub));
	}
	TEST_ASSERT_EQ_STR("603",
		protobuf_unittest_TestAllTypes_reader_get_oneof_string(&r));
	TEST_ASSERT_EQ_STR("604",
		protobuf_unittest_TestAllTypes_reader_get_oneof_bytes(&r));

	free(data);
	return 0;
}

/* `test "repeated types - marshal and parse"` — round-trip every
 * repeated field kind with min/max values. Main.zig:711. */
static int
test_repeated_roundtrip(void)
{
	int32_t rep_int32[]    = { -42, 0, 42 };
	int64_t rep_int64[]    = { INT64_MIN, 0, INT64_MAX };
	uint32_t rep_uint32[]  = { 0, 42, UINT32_MAX };
	uint64_t rep_uint64[]  = { 0, 42, UINT64_MAX };
	int32_t rep_sint32[]   = { INT32_MIN, 0, INT32_MAX };
	int64_t rep_sint64[]   = { INT64_MIN, 0, INT64_MAX };
	uint32_t rep_fixed32[] = { 0, 42, UINT32_MAX };
	uint64_t rep_fixed64[] = { 0, 42, UINT64_MAX };
	int32_t rep_sfixed32[] = { INT32_MIN, 0, INT32_MAX };
	int64_t rep_sfixed64[] = { INT64_MIN, 0, INT64_MAX };
	float rep_float[]      = { -3.4028235e+38f, 0, 3.4028235e+38f };
	double rep_double[]    = { -1.7976931348623157e+308, 0, 1.7976931348623157e+308 };
	bool rep_bool[]        = { true, false, true };
	struct gremlin_bytes rep_string[] = {
		GB_CSTR("hello"), GB_CSTR(""), GB_CSTR("world")
	};
	struct gremlin_bytes rep_bytes[] = {
		GB_CSTR("bytes1"), GB_CSTR(""), GB_CSTR("bytes2")
	};
	struct gremlin_bytes rep_sp[] = {
		GB_CSTR("piece1"), GB_CSTR(""), GB_CSTR("piece2")
	};
	struct gremlin_bytes rep_cord[] = {
		GB_CSTR("cord1"), GB_CSTR(""), GB_CSTR("cord2")
	};

	protobuf_unittest_TestAllTypes_NestedMessage n1 = { .bb = 1 };
	protobuf_unittest_TestAllTypes_NestedMessage n2 = { .bb = 2 };
	protobuf_unittest_TestAllTypes_NestedMessage n3 = { .bb = 3 };
	const protobuf_unittest_TestAllTypes_NestedMessage *rep_nested[] = { &n1, &n2, &n3 };

	protobuf_unittest_TestAllTypes_NestedEnum rep_enum[] = {
		protobuf_unittest_TestAllTypes_NestedEnum_FOO,
		protobuf_unittest_TestAllTypes_NestedEnum_BAR,
		protobuf_unittest_TestAllTypes_NestedEnum_BAZ,
	};

	protobuf_unittest_TestAllTypes msg = {
		.repeated_int32    = rep_int32,    .repeated_int32_count    = 3,
		.repeated_int64    = rep_int64,    .repeated_int64_count    = 3,
		.repeated_uint32   = rep_uint32,   .repeated_uint32_count   = 3,
		.repeated_uint64   = rep_uint64,   .repeated_uint64_count   = 3,
		.repeated_sint32   = rep_sint32,   .repeated_sint32_count   = 3,
		.repeated_sint64   = rep_sint64,   .repeated_sint64_count   = 3,
		.repeated_fixed32  = rep_fixed32,  .repeated_fixed32_count  = 3,
		.repeated_fixed64  = rep_fixed64,  .repeated_fixed64_count  = 3,
		.repeated_sfixed32 = rep_sfixed32, .repeated_sfixed32_count = 3,
		.repeated_sfixed64 = rep_sfixed64, .repeated_sfixed64_count = 3,
		.repeated_float    = rep_float,    .repeated_float_count    = 3,
		.repeated_double   = rep_double,   .repeated_double_count   = 3,
		.repeated_bool     = rep_bool,     .repeated_bool_count     = 3,
		.repeated_string        = rep_string, .repeated_string_count        = 3,
		.repeated_bytes         = rep_bytes,  .repeated_bytes_count         = 3,
		.repeated_string_piece  = rep_sp,     .repeated_string_piece_count  = 3,
		.repeated_cord          = rep_cord,   .repeated_cord_count          = 3,
		.repeated_nested_message = rep_nested, .repeated_nested_message_count = 3,
		.repeated_nested_enum    = rep_enum,   .repeated_nested_enum_count    = 3,
	};

	uint8_t *buf;
	size_t buf_len;
	TEST_ENCODE(protobuf_unittest_TestAllTypes, &msg, buf, buf_len);

	protobuf_unittest_TestAllTypes_reader r;
	enum gremlin_error err = protobuf_unittest_TestAllTypes_reader_init(
		&r, buf, buf_len);
	TEST_ASSERT_EQ_INT(GREMLIN_OK, err);

#define CHECK_RT_NUM(kind, ctype, src) do { \
	protobuf_unittest_TestAllTypes_reader_repeated_##kind##_iter _it = \
		protobuf_unittest_TestAllTypes_reader_repeated_##kind##_begin(&r); \
	ctype _v; size_t _n = 0; \
	while (_it.count_remaining > 0) { \
		err = protobuf_unittest_TestAllTypes_reader_repeated_##kind##_next(&_it, &_v); \
		TEST_ASSERT_EQ_INT(GREMLIN_OK, err); \
		TEST_ASSERT_EQ_FLOAT((double)(src)[_n], (double)_v); \
		_n++; \
	} \
	TEST_ASSERT_EQ_UINT(3, _n); \
} while (0)

	CHECK_RT_NUM(int32,    int32_t,  rep_int32);
	CHECK_RT_NUM(int64,    int64_t,  rep_int64);
	CHECK_RT_NUM(uint32,   uint32_t, rep_uint32);
	CHECK_RT_NUM(uint64,   uint64_t, rep_uint64);
	CHECK_RT_NUM(sint32,   int32_t,  rep_sint32);
	CHECK_RT_NUM(sint64,   int64_t,  rep_sint64);
	CHECK_RT_NUM(fixed32,  uint32_t, rep_fixed32);
	CHECK_RT_NUM(fixed64,  uint64_t, rep_fixed64);
	CHECK_RT_NUM(sfixed32, int32_t,  rep_sfixed32);
	CHECK_RT_NUM(sfixed64, int64_t,  rep_sfixed64);
	CHECK_RT_NUM(float,    float,    rep_float);
	CHECK_RT_NUM(double,   double,   rep_double);
#undef CHECK_RT_NUM

	{
		protobuf_unittest_TestAllTypes_reader_repeated_bool_iter it =
			protobuf_unittest_TestAllTypes_reader_repeated_bool_begin(&r);
		bool v; size_t n = 0;
		while (it.count_remaining > 0) {
			err = protobuf_unittest_TestAllTypes_reader_repeated_bool_next(&it, &v);
			TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
			TEST_ASSERT_EQ_BOOL(rep_bool[n], v);
			n++;
		}
		TEST_ASSERT_EQ_UINT(3, n);
	}

#define CHECK_RT_BYTES(kind, src) do { \
	protobuf_unittest_TestAllTypes_reader_repeated_##kind##_iter _it = \
		protobuf_unittest_TestAllTypes_reader_repeated_##kind##_begin(&r); \
	struct gremlin_bytes _v; size_t _n = 0; \
	while (_it.count_remaining > 0) { \
		err = protobuf_unittest_TestAllTypes_reader_repeated_##kind##_next(&_it, &_v); \
		TEST_ASSERT_EQ_INT(GREMLIN_OK, err); \
		TEST_ASSERT_EQ_UINT((src)[_n].len, _v.len); \
		if (_v.len > 0) TEST_ASSERT(memcmp((src)[_n].data, _v.data, _v.len) == 0); \
		_n++; \
	} \
	TEST_ASSERT_EQ_UINT(3, _n); \
} while (0)

	CHECK_RT_BYTES(string,       rep_string);
	CHECK_RT_BYTES(bytes,        rep_bytes);
	CHECK_RT_BYTES(string_piece, rep_sp);
	CHECK_RT_BYTES(cord,         rep_cord);
#undef CHECK_RT_BYTES

	{
		protobuf_unittest_TestAllTypes_reader_repeated_nested_message_iter it =
			protobuf_unittest_TestAllTypes_reader_repeated_nested_message_begin(&r);
		protobuf_unittest_TestAllTypes_NestedMessage_reader sub;
		size_t n = 0;
		while (it.count_remaining > 0) {
			err = protobuf_unittest_TestAllTypes_reader_repeated_nested_message_next(&it, &sub);
			TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
			TEST_ASSERT_EQ_INT(rep_nested[n]->bb,
				protobuf_unittest_TestAllTypes_NestedMessage_reader_get_bb(&sub));
			n++;
		}
		TEST_ASSERT_EQ_UINT(3, n);
	}

	{
		protobuf_unittest_TestAllTypes_reader_repeated_nested_enum_iter it =
			protobuf_unittest_TestAllTypes_reader_repeated_nested_enum_begin(&r);
		protobuf_unittest_TestAllTypes_NestedEnum v; size_t n = 0;
		while (it.count_remaining > 0) {
			err = protobuf_unittest_TestAllTypes_reader_repeated_nested_enum_next(&it, &v);
			TEST_ASSERT_EQ_INT(GREMLIN_OK, err);
			TEST_ASSERT_EQ_INT(rep_enum[n], v);
			n++;
		}
		TEST_ASSERT_EQ_UINT(3, n);
	}

	free(buf);
	return 0;
}

/* `test "nil list"` — nil elements in a repeated-message field encode
 * as empty sub-messages (length 0). Main.zig:467. */
static const uint8_t NIL_LIST_EXPECTED[] = {
	202, 3, 0, 202, 3, 2, 8, 1, 202, 3, 0,
	232, 3, 0, 240, 3, 0, 248, 3, 0, 128, 4, 0, 136, 4, 0, 144, 4, 0,
	157, 4, 0, 0, 0, 0, 161, 4, 0, 0, 0, 0, 0, 0, 0, 0,
	173, 4, 0, 0, 0, 0, 177, 4, 0, 0, 0, 0, 0, 0, 0, 0,
	189, 4, 0, 0, 0, 0, 193, 4, 0, 0, 0, 0, 0, 0, 0, 0,
	200, 4, 0, 210, 4, 0, 218, 4, 0, 136, 5, 0, 144, 5, 0, 152, 5, 0,
	162, 5, 0, 170, 5, 0
};

static int
test_nil_list(void)
{
	protobuf_unittest_TestAllTypes_NestedMessage one = { .bb = 1 };
	const protobuf_unittest_TestAllTypes_NestedMessage *lazy[] = {
		NULL, &one, NULL
	};
	protobuf_unittest_TestAllTypes msg = {
		.repeated_lazy_message = lazy,
		.repeated_lazy_message_count = 3,
	};

	uint8_t *buf;
	size_t buf_len;
	TEST_ENCODE(protobuf_unittest_TestAllTypes, &msg, buf, buf_len);

	TEST_ASSERT_EQ_BYTES(NIL_LIST_EXPECTED, sizeof NIL_LIST_EXPECTED,
	                     buf, buf_len);
	free(buf);
	return 0;
}

/* `test "ambigious ref"` — whatsapp proto's top-level `Account` name
 * collides with nested `Account` definitions in other messages; make
 * sure encode still runs without linker/name issues. Main.zig:959. */
static int
test_ambigious_ref(void)
{
	Account acc = { .is_username_deleted = true };

	uint8_t *buf;
	size_t buf_len;
	TEST_ENCODE(Account, &acc, buf, buf_len);
	/* Zig doesn't assert exact bytes — just that encode completes. */
	(void)buf_len;
	free(buf);
	return 0;
}

/* ------------------------------------------------------------------
 * Test registry + runner
 * ------------------------------------------------------------------ */

struct test_case {
	const char	*name;
	int		(*fn)(void);
};

static const struct test_case TESTS[] = {
	{ "simple write",    test_simple_write    },
	{ "simple read",     test_simple_read     },
	{ "map kv: empty",   test_map_kv_empty    },
	{ "map kv: value",   test_map_kv_value    },
	{ "negative values", test_negative_values },
	{ "complex write",   test_complex_write   },
	{ "complex read",    test_complex_read    },
	{ "nil list",        test_nil_list        },
	{ "map parsing",     test_map_parsing     },
	{ "golden message",  test_golden_message  },
	{ "repeated types - marshal and parse", test_repeated_roundtrip },
	{ "ambigious ref",   test_ambigious_ref   },
};

int
main(void)
{
	size_t n = sizeof TESTS / sizeof TESTS[0];
	size_t passed = 0;
	size_t failed = 0;

	for (size_t i = 0; i < n; i++) {
		fprintf(stderr, "[%zu/%zu] %s ... ", i + 1, n, TESTS[i].name);
		int r = TESTS[i].fn();
		if (r == 0) {
			fprintf(stderr, "ok\n");
			passed++;
		} else {
			fprintf(stderr, "FAILED\n");
			failed++;
		}
	}

	fprintf(stderr, "\n%zu passed, %zu failed, %zu total\n",
		passed, failed, n);
	return failed == 0 ? 0 : 1;
}

#include "tests.h"
#include "gremlinp/lib.h"

void integration_test(void);

static struct gremlinp_parser_buffer make_buf(char *str) {
    struct gremlinp_parser_buffer pb;
    gremlinp_parser_buffer_init(&pb, str, 0);
    return pb;
}

static struct gremlinp_parser_buffer make_body(
    struct gremlinp_parser_buffer *parent,
    size_t body_start, size_t body_end)
{
    struct gremlinp_parser_buffer body;
    gremlinp_parser_buffer_init(&body, parent->buf, body_start);
    body.buf_size = body_end;
    return body;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

/* ========================================================================
 * Test 1: Full proto3 file — parse every top-level entry and check values
 * ======================================================================== */

TEST(integration_test_full_proto3)
{
    char proto[] =
        "syntax = \"proto3\";\n"
        "\n"
        "package example.api;\n"
        "\n"
        "import \"google/protobuf/timestamp.proto\";\n"
        "import public \"other.proto\";\n"
        "\n"
        "option java_package = \"com.example.api\";\n"
        "option optimize_for = SPEED;\n"
        "\n"
        "enum Status {\n"
        "  UNKNOWN = 0;\n"
        "  ACTIVE = 1;\n"
        "  INACTIVE = 2;\n"
        "}\n"
        "\n"
        "message User {\n"
        "  string name = 1;\n"
        "  int32 id = 2;\n"
        "  Status status = 3;\n"
        "  repeated string tags = 4;\n"
        "  map<string, string> metadata = 5;\n"
        "  oneof contact {\n"
        "    string email = 6;\n"
        "    string phone = 7;\n"
        "  }\n"
        "  message Address {\n"
        "    string street = 1;\n"
        "    string city = 2;\n"
        "  }\n"
        "  Address address = 8;\n"
        "  reserved 100, 200 to 300;\n"
        "  reserved \"old_field\";\n"
        "}\n"
        "\n"
        "service UserService {\n"
        "  rpc GetUser (User) returns (User) {}\n"
        "}\n";

    struct gremlinp_parser_buffer buf = make_buf(proto);
    struct gremlinp_file_entry_result fe;

    /* syntax */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_SYNTAX, fe.kind);
    ASSERT_SPAN_EQ("proto3", fe.u.syntax.version_start, fe.u.syntax.version_length);

    /* package */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_PACKAGE, fe.kind);
    ASSERT_SPAN_EQ("example.api", fe.u.package.name_start, fe.u.package.name_length);

    /* import regular */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_IMPORT, fe.kind);
    ASSERT_EQ(GREMLINP_IMPORT_TYPE_REGULAR, fe.u.import.type);
    ASSERT_SPAN_EQ("google/protobuf/timestamp.proto", fe.u.import.path_start, fe.u.import.path_length);

    /* import public */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_IMPORT, fe.kind);
    ASSERT_EQ(GREMLINP_IMPORT_TYPE_PUBLIC, fe.u.import.type);
    ASSERT_SPAN_EQ("other.proto", fe.u.import.path_start, fe.u.import.path_length);

    /* option java_package */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_OPTION, fe.kind);
    ASSERT_SPAN_EQ("java_package", fe.u.option.name_start, fe.u.option.name_length);
    ASSERT_EQ(GREMLINP_CONST_STRING, fe.u.option.value.kind);

    /* option optimize_for */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_OPTION, fe.kind);
    ASSERT_SPAN_EQ("optimize_for", fe.u.option.name_start, fe.u.option.name_length);
    ASSERT_EQ(GREMLINP_CONST_IDENTIFIER, fe.u.option.value.kind);

    /* enum Status */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_ENUM, fe.kind);
    ASSERT_SPAN_EQ("Status", fe.u.enumeration.name_start, fe.u.enumeration.name_length);

    /* message User */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_MESSAGE, fe.kind);
    ASSERT_SPAN_EQ("User", fe.u.message.name_start, fe.u.message.name_length);

    /* service UserService */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_SERVICE, fe.kind);

    /* EOF */
    gremlinp_parser_buffer_skip_spaces(&buf);
    ASSERT_EQ('\0', gremlinp_parser_buffer_char(&buf));
}

/* ========================================================================
 * Test 2: Enum body iteration — option, fields, reserved, check all values
 * ======================================================================== */

TEST(integration_test_enum_detailed)
{
    char proto[] =
        "enum Color {\n"
        "  option allow_alias = true;\n"
        "  RED = 0;\n"
        "  GREEN = 1;\n"
        "  BLUE = 2;\n"
        "  AZURE = 2;\n"
        "  reserved 10 to 20;\n"
        "}\n";

    struct gremlinp_parser_buffer buf = make_buf(proto);
    struct gremlinp_enum_parse_result en = gremlinp_enum_parse(&buf);
    ASSERT_EQ(GREMLINP_OK, en.error);
    ASSERT_SPAN_EQ("Color", en.name_start, en.name_length);

    struct gremlinp_parser_buffer body = make_body(&buf, en.body_start, en.body_end);

    /* option allow_alias = true */
    struct gremlinp_enum_entry_result e1 = gremlinp_enum_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, e1.error);
    ASSERT_EQ(GREMLINP_ENUM_ENTRY_OPTION, e1.kind);
    ASSERT_SPAN_EQ("allow_alias", e1.u.option.name_start, e1.u.option.name_length);

    /* RED = 0 */
    struct gremlinp_enum_entry_result e2 = gremlinp_enum_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, e2.error);
    ASSERT_EQ(GREMLINP_ENUM_ENTRY_FIELD, e2.kind);
    ASSERT_SPAN_EQ("RED", e2.u.field.name_start, e2.u.field.name_length);
    ASSERT_EQ(0, e2.u.field.index);

    /* GREEN = 1 */
    struct gremlinp_enum_entry_result e3 = gremlinp_enum_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, e3.error);
    ASSERT_EQ(GREMLINP_ENUM_ENTRY_FIELD, e3.kind);
    ASSERT_SPAN_EQ("GREEN", e3.u.field.name_start, e3.u.field.name_length);
    ASSERT_EQ(1, e3.u.field.index);

    /* BLUE = 2 */
    struct gremlinp_enum_entry_result e4 = gremlinp_enum_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, e4.error);
    ASSERT_EQ(GREMLINP_ENUM_ENTRY_FIELD, e4.kind);
    ASSERT_SPAN_EQ("BLUE", e4.u.field.name_start, e4.u.field.name_length);
    ASSERT_EQ(2, e4.u.field.index);

    /* AZURE = 2 (alias) */
    struct gremlinp_enum_entry_result e5 = gremlinp_enum_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, e5.error);
    ASSERT_EQ(GREMLINP_ENUM_ENTRY_FIELD, e5.kind);
    ASSERT_SPAN_EQ("AZURE", e5.u.field.name_start, e5.u.field.name_length);
    ASSERT_EQ(2, e5.u.field.index);

    /* reserved 10 to 20 */
    struct gremlinp_enum_entry_result e6 = gremlinp_enum_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, e6.error);
    ASSERT_EQ(GREMLINP_ENUM_ENTRY_RESERVED, e6.kind);
    ASSERT_EQ(GREMLINP_RESERVED_RANGES, e6.u.reserved.kind);

    /* end of body */
    gremlinp_parser_buffer_skip_spaces(&body);
    ASSERT_EQ(body.offset, body.buf_size);
}

/* ========================================================================
 * Test 3: Message body — all field types, labels, indices
 * ======================================================================== */

TEST(integration_test_message_all_fields)
{
    char proto[] =
        "message TestAllTypes {\n"
        "  int32 f_int32 = 1;\n"
        "  int64 f_int64 = 2;\n"
        "  uint32 f_uint32 = 3;\n"
        "  uint64 f_uint64 = 4;\n"
        "  sint32 f_sint32 = 5;\n"
        "  sint64 f_sint64 = 6;\n"
        "  fixed32 f_fixed32 = 7;\n"
        "  fixed64 f_fixed64 = 8;\n"
        "  sfixed32 f_sfixed32 = 9;\n"
        "  sfixed64 f_sfixed64 = 10;\n"
        "  float f_float = 11;\n"
        "  double f_double = 12;\n"
        "  bool f_bool = 13;\n"
        "  string f_string = 14;\n"
        "  bytes f_bytes = 15;\n"
        "  NestedMessage f_nested = 16;\n"
        "  optional string f_optional = 17;\n"
        "  repeated int32 f_repeated = 18;\n"
        "  map<string, int32> f_map_si = 19;\n"
        "  map<int64, NestedMessage> f_map_msg = 20;\n"
        "  string f_with_opts = 21 [deprecated = true];\n"
        "}\n";

    struct gremlinp_parser_buffer buf = make_buf(proto);
    struct gremlinp_message_parse_result msg = gremlinp_message_parse(&buf);
    ASSERT_EQ(GREMLINP_OK, msg.error);
    ASSERT_SPAN_EQ("TestAllTypes", msg.name_start, msg.name_length);

    struct gremlinp_parser_buffer body = make_body(&buf, msg.body_start, msg.body_end);
    struct gremlinp_message_entry_result me;

    /* Scalar fields: f_int32(1) .. f_bytes(15), f_nested(16) */
    struct { const char *name; int32_t idx; } scalars[] = {
        {"f_int32", 1},  {"f_int64", 2},  {"f_uint32", 3},  {"f_uint64", 4},
        {"f_sint32", 5}, {"f_sint64", 6}, {"f_fixed32", 7}, {"f_fixed64", 8},
        {"f_sfixed32", 9}, {"f_sfixed64", 10}, {"f_float", 11}, {"f_double", 12},
        {"f_bool", 13},  {"f_string", 14}, {"f_bytes", 15}, {"f_nested", 16}
    };
    size_t si;
    for (si = 0; si < sizeof(scalars) / sizeof(scalars[0]); si++) {
        me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
        ASSERT_EQ(GREMLINP_FIELD_LABEL_NONE, me.u.field.label);
        ASSERT_SPAN_EQ(scalars[si].name, me.u.field.name_start, me.u.field.name_length);
        ASSERT_EQ(scalars[si].idx, me.u.field.index);
    }

    /* optional string f_optional = 17 */
    me = gremlinp_message_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, me.error);
    ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
    ASSERT_EQ(GREMLINP_FIELD_LABEL_OPTIONAL, me.u.field.label);
    ASSERT_SPAN_EQ("f_optional", me.u.field.name_start, me.u.field.name_length);
    ASSERT_EQ(17, me.u.field.index);

    /* repeated int32 f_repeated = 18 */
    me = gremlinp_message_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, me.error);
    ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
    ASSERT_EQ(GREMLINP_FIELD_LABEL_REPEATED, me.u.field.label);
    ASSERT_EQ(GREMLINP_FIELD_TYPE_NAMED, me.u.field.type.kind);
    ASSERT_SPAN_EQ("f_repeated", me.u.field.name_start, me.u.field.name_length);
    ASSERT_EQ(18, me.u.field.index);

    /* map<string, int32> f_map_si = 19 */
    me = gremlinp_message_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, me.error);
    ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
    ASSERT_EQ(GREMLINP_FIELD_TYPE_MAP, me.u.field.type.kind);
    ASSERT_SPAN_EQ("f_map_si", me.u.field.name_start, me.u.field.name_length);
    ASSERT_EQ(19, me.u.field.index);
    ASSERT_SPAN_EQ("string", me.u.field.type.u.map.key_type.start, me.u.field.type.u.map.key_type.length);
    ASSERT_SPAN_EQ("int32", me.u.field.type.u.map.value_type.start, me.u.field.type.u.map.value_type.length);

    /* map<int64, NestedMessage> f_map_msg = 20 */
    me = gremlinp_message_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, me.error);
    ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
    ASSERT_EQ(GREMLINP_FIELD_TYPE_MAP, me.u.field.type.kind);
    ASSERT_SPAN_EQ("f_map_msg", me.u.field.name_start, me.u.field.name_length);
    ASSERT_EQ(20, me.u.field.index);
    ASSERT_SPAN_EQ("int64", me.u.field.type.u.map.key_type.start, me.u.field.type.u.map.key_type.length);
    ASSERT_SPAN_EQ("NestedMessage", me.u.field.type.u.map.value_type.start, me.u.field.type.u.map.value_type.length);

    /* string f_with_opts = 21 [deprecated = true] */
    me = gremlinp_message_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, me.error);
    ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
    ASSERT_SPAN_EQ("f_with_opts", me.u.field.name_start, me.u.field.name_length);
    ASSERT_EQ(21, me.u.field.index);
    ASSERT_EQ(GREMLINP_OK, me.u.field.options.error);
    ASSERT_EQ(1, me.u.field.options.count);

    /* end of body */
    gremlinp_parser_buffer_skip_spaces(&body);
    ASSERT_EQ(body.offset, body.buf_size);
}

/* ========================================================================
 * Test 4: Nested message and enum within a message
 * ======================================================================== */

TEST(integration_test_nested_structures)
{
    char proto[] =
        "message Outer {\n"
        "  enum InnerEnum {\n"
        "    FOO = 0;\n"
        "    BAR = 1;\n"
        "    NEG = -1;\n"
        "  }\n"
        "  message InnerMsg {\n"
        "    int32 a = 1;\n"
        "    Outer corecursive = 2;\n"
        "  }\n"
        "  InnerEnum status = 1;\n"
        "  InnerMsg child = 2;\n"
        "  reserved 100, 200 to 300;\n"
        "  reserved \"removed_field\";\n"
        "}\n";

    struct gremlinp_parser_buffer buf = make_buf(proto);
    struct gremlinp_message_parse_result msg = gremlinp_message_parse(&buf);
    ASSERT_EQ(GREMLINP_OK, msg.error);
    ASSERT_SPAN_EQ("Outer", msg.name_start, msg.name_length);

    struct gremlinp_parser_buffer body = make_body(&buf, msg.body_start, msg.body_end);

    /* entry 1: enum InnerEnum */
    struct gremlinp_message_entry_result me = gremlinp_message_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, me.error);
    ASSERT_EQ(GREMLINP_MSG_ENTRY_ENUM, me.kind);
    ASSERT_SPAN_EQ("InnerEnum", me.u.enumeration.name_start, me.u.enumeration.name_length);
    {
        struct gremlinp_parser_buffer ebody = make_body(&buf, me.u.enumeration.body_start, me.u.enumeration.body_end);

        struct gremlinp_enum_entry_result ef = gremlinp_enum_next_entry(&ebody);
        ASSERT_EQ(GREMLINP_OK, ef.error);
        ASSERT_EQ(GREMLINP_ENUM_ENTRY_FIELD, ef.kind);
        ASSERT_SPAN_EQ("FOO", ef.u.field.name_start, ef.u.field.name_length);
        ASSERT_EQ(0, ef.u.field.index);

        ef = gremlinp_enum_next_entry(&ebody);
        ASSERT_EQ(GREMLINP_OK, ef.error);
        ASSERT_EQ(GREMLINP_ENUM_ENTRY_FIELD, ef.kind);
        ASSERT_SPAN_EQ("BAR", ef.u.field.name_start, ef.u.field.name_length);
        ASSERT_EQ(1, ef.u.field.index);

        ef = gremlinp_enum_next_entry(&ebody);
        ASSERT_EQ(GREMLINP_OK, ef.error);
        ASSERT_EQ(GREMLINP_ENUM_ENTRY_FIELD, ef.kind);
        ASSERT_SPAN_EQ("NEG", ef.u.field.name_start, ef.u.field.name_length);
        ASSERT_EQ(-1, ef.u.field.index);
    }

    /* entry 2: message InnerMsg */
    me = gremlinp_message_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, me.error);
    ASSERT_EQ(GREMLINP_MSG_ENTRY_MESSAGE, me.kind);
    ASSERT_SPAN_EQ("InnerMsg", me.u.message.name_start, me.u.message.name_length);
    {
        struct gremlinp_parser_buffer imbody = make_body(&buf, me.u.message.body_start, me.u.message.body_end);
        struct gremlinp_message_entry_result ime;

        ime = gremlinp_message_next_entry(&imbody);
        ASSERT_EQ(GREMLINP_OK, ime.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, ime.kind);
        ASSERT_SPAN_EQ("a", ime.u.field.name_start, ime.u.field.name_length);
        ASSERT_EQ(1, ime.u.field.index);

        ime = gremlinp_message_next_entry(&imbody);
        ASSERT_EQ(GREMLINP_OK, ime.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, ime.kind);
        ASSERT_SPAN_EQ("corecursive", ime.u.field.name_start, ime.u.field.name_length);
        ASSERT_EQ(2, ime.u.field.index);
    }

    /* entry 3: field InnerEnum status = 1 */
    me = gremlinp_message_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, me.error);
    ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);

    /* entry 4: field InnerMsg child = 2 */
    me = gremlinp_message_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, me.error);
    ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);

    /* entry 5: reserved 100, 200 to 300 */
    me = gremlinp_message_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, me.error);
    ASSERT_EQ(GREMLINP_MSG_ENTRY_RESERVED, me.kind);

    /* entry 6: reserved "removed_field" */
    me = gremlinp_message_next_entry(&body);
    ASSERT_EQ(GREMLINP_OK, me.error);
    ASSERT_EQ(GREMLINP_MSG_ENTRY_RESERVED, me.kind);

    /* end of body */
    gremlinp_parser_buffer_skip_spaces(&body);
    ASSERT_EQ(body.offset, body.buf_size);
}

/* ========================================================================
 * Test 5: Oneof iteration — check field names, types, indices
 * ======================================================================== */

TEST(integration_test_oneof_iteration)
{
    char proto[] =
        "oneof test_oneof {\n"
        "  uint32 oneof_uint32 = 111;\n"
        "  NestedMessage oneof_nested = 112;\n"
        "  string oneof_string = 113;\n"
        "  bytes oneof_bytes = 114;\n"
        "  bool oneof_bool = 115;\n"
        "  uint64 oneof_uint64 = 116;\n"
        "  float oneof_float = 117;\n"
        "  double oneof_double = 118;\n"
        "  NestedEnum oneof_enum = 119;\n"
        "  google.protobuf.NullValue oneof_null = 120;\n"
        "}\n";

    struct gremlinp_parser_buffer buf = make_buf(proto);
    struct gremlinp_oneof_parse_result oo = gremlinp_oneof_parse(&buf);
    ASSERT_EQ(GREMLINP_OK, oo.error);
    ASSERT_SPAN_EQ("test_oneof", oo.name_start, oo.name_length);

    struct gremlinp_parser_buffer body = make_body(&buf, oo.body_start, oo.body_end);

    struct {
        const char *type;
        const char *name;
        int32_t index;
    } expected[] = {
        {"uint32",                       "oneof_uint32",  111},
        {"NestedMessage",                "oneof_nested",  112},
        {"string",                       "oneof_string",  113},
        {"bytes",                        "oneof_bytes",   114},
        {"bool",                         "oneof_bool",    115},
        {"uint64",                       "oneof_uint64",  116},
        {"float",                        "oneof_float",   117},
        {"double",                       "oneof_double",  118},
        {"NestedEnum",                   "oneof_enum",    119},
        {"google.protobuf.NullValue",    "oneof_null",    120}
    };

    size_t i;
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        struct gremlinp_oneof_entry_result e = gremlinp_oneof_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, e.error);
        ASSERT_EQ(GREMLINP_ONEOF_ENTRY_FIELD, e.kind);
        ASSERT_SPAN_EQ(expected[i].type, e.u.field.type_name.start, e.u.field.type_name.length);
        ASSERT_SPAN_EQ(expected[i].name, e.u.field.name_start, e.u.field.name_length);
        ASSERT_EQ(expected[i].index, e.u.field.index);
    }

    gremlinp_parser_buffer_skip_spaces(&body);
    ASSERT_EQ(body.offset, body.buf_size);
}

/* ========================================================================
 * Test 6: Map field types — key and value type spans
 * ======================================================================== */

TEST(integration_test_map_fields)
{
    struct {
        const char *input;
        const char *key;
        const char *value;
        const char *name;
        int32_t index;
    } cases[] = {
        {"map<int32, int32> m1 = 1;",       "int32",  "int32",          "m1", 1},
        {"map<int64, int64> m2 = 2;",       "int64",  "int64",          "m2", 2},
        {"map<string, string> m3 = 3;",     "string", "string",         "m3", 3},
        {"map<string, bytes> m4 = 4;",      "string", "bytes",          "m4", 4},
        {"map<string, NestedMsg> m5 = 5;",  "string", "NestedMsg",      "m5", 5},
        {"map<bool, bool> m6 = 6;",         "bool",   "bool",           "m6", 6},
        {"map<fixed32, float> m7 = 7;",     "fixed32","float",          "m7", 7},
        {"map<sfixed64, double> m8 = 8;",   "sfixed64","double",        "m8", 8},
        {"map<sint32, ForeignEnum> m9 = 9;","sint32", "ForeignEnum",    "m9", 9},
    };

    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t len = strlen(cases[i].input);
        char *s = malloc(len + 1);
        ASSERT_NOT_NULL(s);
        memcpy(s, cases[i].input, len + 1);

        struct gremlinp_parser_buffer buf = make_buf(s);
        struct gremlinp_field_parse_result f = gremlinp_field_parse(&buf);
        ASSERT_EQ(GREMLINP_OK, f.error);
        ASSERT_EQ(GREMLINP_FIELD_TYPE_MAP, f.type.kind);
        ASSERT_SPAN_EQ(cases[i].key, f.type.u.map.key_type.start, f.type.u.map.key_type.length);
        ASSERT_SPAN_EQ(cases[i].value, f.type.u.map.value_type.start, f.type.u.map.value_type.length);
        ASSERT_SPAN_EQ(cases[i].name, f.name_start, f.name_length);
        ASSERT_EQ(cases[i].index, f.index);

        free(s);
    }
}

/* ========================================================================
 * Test 7: File-level iteration using file_next_entry
 * ======================================================================== */

TEST(integration_test_file_entry_iterator)
{
    char proto[] =
        "syntax = \"proto3\";\n"
        "package test;\n"
        "import \"a.proto\";\n"
        "option go_package = \"test\";\n"
        "enum E { V = 0; }\n"
        "message M { int32 x = 1; }\n"
        "service S { rpc F (M) returns (M) {} }\n";

    struct gremlinp_parser_buffer buf = make_buf(proto);

    enum gremlinp_file_entry_kind expected_kinds[] = {
        GREMLINP_FILE_ENTRY_SYNTAX,
        GREMLINP_FILE_ENTRY_PACKAGE,
        GREMLINP_FILE_ENTRY_IMPORT,
        GREMLINP_FILE_ENTRY_OPTION,
        GREMLINP_FILE_ENTRY_ENUM,
        GREMLINP_FILE_ENTRY_MESSAGE,
        GREMLINP_FILE_ENTRY_SERVICE
    };

    size_t i;
    for (i = 0; i < sizeof(expected_kinds) / sizeof(expected_kinds[0]); i++) {
        struct gremlinp_file_entry_result e = gremlinp_file_next_entry(&buf);
        ASSERT_EQ(GREMLINP_OK, e.error);
        ASSERT_EQ(expected_kinds[i], e.kind);
        ASSERT_TRUE(e.end > e.start);
    }

    /* EOF */
    gremlinp_parser_buffer_skip_spaces(&buf);
    ASSERT_EQ('\0', gremlinp_parser_buffer_char(&buf));
}

/* ========================================================================
 * Test 8: Message entry iterator — checks all entry kinds
 * ======================================================================== */

TEST(integration_test_message_entry_iterator)
{
    char proto[] =
        "message Full {\n"
        "  enum Color { RED = 0; }\n"
        "  message Inner { int32 x = 1; }\n"
        "  oneof choice {\n"
        "    string a = 1;\n"
        "    int32 b = 2;\n"
        "  }\n"
        "  option deprecated = true;\n"
        "  reserved 50 to 60;\n"
        "  string name = 3;\n"
        "  repeated int32 ids = 4;\n"
        "  map<string, Inner> lookup = 5;\n"
        "}\n";

    struct gremlinp_parser_buffer buf = make_buf(proto);
    struct gremlinp_message_parse_result msg = gremlinp_message_parse(&buf);
    ASSERT_EQ(GREMLINP_OK, msg.error);
    ASSERT_SPAN_EQ("Full", msg.name_start, msg.name_length);

    struct gremlinp_parser_buffer body = make_body(&buf, msg.body_start, msg.body_end);

    enum gremlinp_message_entry_kind expected_kinds[] = {
        GREMLINP_MSG_ENTRY_ENUM,
        GREMLINP_MSG_ENTRY_MESSAGE,
        GREMLINP_MSG_ENTRY_ONEOF,
        GREMLINP_MSG_ENTRY_OPTION,
        GREMLINP_MSG_ENTRY_RESERVED,
        GREMLINP_MSG_ENTRY_FIELD,
        GREMLINP_MSG_ENTRY_FIELD,
        GREMLINP_MSG_ENTRY_FIELD
    };

    size_t i;
    for (i = 0; i < sizeof(expected_kinds) / sizeof(expected_kinds[0]); i++) {
        struct gremlinp_message_entry_result me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(expected_kinds[i], me.kind);
    }

    gremlinp_parser_buffer_skip_spaces(&body);
    ASSERT_EQ(body.offset, body.buf_size);
}

/* ========================================================================
 * Test 9: Edition syntax (proto editions)
 * ======================================================================== */

TEST(integration_test_edition)
{
    char proto[] =
        "edition = \"2023\";\n"
        "package test.editions;\n"
        "option features.field_presence = IMPLICIT;\n"
        "message Msg { int32 x = 1; }\n";

    struct gremlinp_parser_buffer buf = make_buf(proto);
    struct gremlinp_file_entry_result fe;

    /* edition */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_EDITION, fe.kind);
    ASSERT_SPAN_EQ("2023", fe.u.edition.edition_start, fe.u.edition.edition_length);

    /* package */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_PACKAGE, fe.kind);
    ASSERT_SPAN_EQ("test.editions", fe.u.package.name_start, fe.u.package.name_length);

    /* option */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_OPTION, fe.kind);
    ASSERT_SPAN_EQ("features.field_presence", fe.u.option.name_start, fe.u.option.name_length);
    ASSERT_EQ(GREMLINP_CONST_IDENTIFIER, fe.u.option.value.kind);

    /* message */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_MESSAGE, fe.kind);
    ASSERT_SPAN_EQ("Msg", fe.u.message.name_start, fe.u.message.name_length);

    /* EOF */
    gremlinp_parser_buffer_skip_spaces(&buf);
    ASSERT_EQ('\0', gremlinp_parser_buffer_char(&buf));
}

/* ========================================================================
 * Test 10: Golden proto3.proto file — read from disk, parse, check all values
 * ======================================================================== */

TEST(integration_test_golden_proto3)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/google/proto3.proto", TEST_DATA_DIR);
    char *content = read_file(path);
    ASSERT_NOT_NULL(content);

    struct gremlinp_parser_buffer buf = make_buf(content);
    struct gremlinp_file_entry_result fe;

    /* ---- edition = "2023" ---- */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_EDITION, fe.kind);
    ASSERT_SPAN_EQ("2023", fe.u.edition.edition_start, fe.u.edition.edition_length);

    /* ---- package ---- */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_PACKAGE, fe.kind);
    ASSERT_SPAN_EQ("protobuf_test_messages.editions.proto3", fe.u.package.name_start, fe.u.package.name_length);

    /* ---- 6 imports ---- */
    const char *expected_imports[] = {
        "protobuf/any.proto",
        "protobuf/duration.proto",
        "protobuf/field_mask.proto",
        "protobuf/struct.proto",
        "protobuf/timestamp.proto",
        "protobuf/wrappers.proto"
    };
    size_t ii;
    for (ii = 0; ii < 6; ii++) {
        fe = gremlinp_file_next_entry(&buf);
        ASSERT_EQ(GREMLINP_OK, fe.error);
        ASSERT_EQ(GREMLINP_FILE_ENTRY_IMPORT, fe.kind);
        ASSERT_EQ(GREMLINP_IMPORT_TYPE_REGULAR, fe.u.import.type);
        ASSERT_SPAN_EQ(expected_imports[ii], fe.u.import.path_start, fe.u.import.path_length);
    }

    /* ---- 4 options ---- */
    struct { const char *name; enum gremlinp_const_kind vkind; } expected_opts[] = {
        {"features.field_presence", GREMLINP_CONST_IDENTIFIER},
        {"java_package",           GREMLINP_CONST_STRING},
        {"objc_class_prefix",      GREMLINP_CONST_STRING},
        {"optimize_for",           GREMLINP_CONST_IDENTIFIER}
    };
    size_t oi;
    for (oi = 0; oi < 4; oi++) {
        fe = gremlinp_file_next_entry(&buf);
        ASSERT_EQ(GREMLINP_OK, fe.error);
        ASSERT_EQ(GREMLINP_FILE_ENTRY_OPTION, fe.kind);
        ASSERT_SPAN_EQ(expected_opts[oi].name, fe.u.option.name_start, fe.u.option.name_length);
        ASSERT_EQ(expected_opts[oi].vkind, fe.u.option.value.kind);
    }

    /* ---- message TestAllTypesProto3 ---- */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_MESSAGE, fe.kind);
    ASSERT_SPAN_EQ("TestAllTypesProto3", fe.u.message.name_start, fe.u.message.name_length);

    /* Iterate TestAllTypesProto3 body */
    {
        struct gremlinp_parser_buffer body = make_body(&buf, fe.u.message.body_start, fe.u.message.body_end);
        struct gremlinp_message_entry_result me;

        /* NestedMessage */
        me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_MESSAGE, me.kind);
        ASSERT_SPAN_EQ("NestedMessage", me.u.message.name_start, me.u.message.name_length);
        {
            struct gremlinp_parser_buffer nb = make_body(&buf, me.u.message.body_start, me.u.message.body_end);
            struct gremlinp_message_entry_result nme;

            nme = gremlinp_message_next_entry(&nb);
            ASSERT_EQ(GREMLINP_OK, nme.error);
            ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, nme.kind);
            ASSERT_SPAN_EQ("a", nme.u.field.name_start, nme.u.field.name_length);
            ASSERT_EQ(1, nme.u.field.index);

            nme = gremlinp_message_next_entry(&nb);
            ASSERT_EQ(GREMLINP_OK, nme.error);
            ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, nme.kind);
            ASSERT_SPAN_EQ("corecursive", nme.u.field.name_start, nme.u.field.name_length);
            ASSERT_EQ(2, nme.u.field.index);
        }

        /* NestedEnum */
        me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_ENUM, me.kind);
        ASSERT_SPAN_EQ("NestedEnum", me.u.enumeration.name_start, me.u.enumeration.name_length);
        {
            struct gremlinp_parser_buffer eb = make_body(&buf, me.u.enumeration.body_start, me.u.enumeration.body_end);
            struct gremlinp_enum_entry_result ef;

            ef = gremlinp_enum_next_entry(&eb);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_SPAN_EQ("FOO", ef.u.field.name_start, ef.u.field.name_length);
            ASSERT_EQ(0, ef.u.field.index);

            ef = gremlinp_enum_next_entry(&eb);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_SPAN_EQ("BAR", ef.u.field.name_start, ef.u.field.name_length);
            ASSERT_EQ(1, ef.u.field.index);

            ef = gremlinp_enum_next_entry(&eb);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_SPAN_EQ("BAZ", ef.u.field.name_start, ef.u.field.name_length);
            ASSERT_EQ(2, ef.u.field.index);

            ef = gremlinp_enum_next_entry(&eb);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_SPAN_EQ("NEG", ef.u.field.name_start, ef.u.field.name_length);
            ASSERT_EQ(-1, ef.u.field.index);
        }

        /* AliasedEnum */
        me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_ENUM, me.kind);
        ASSERT_SPAN_EQ("AliasedEnum", me.u.enumeration.name_start, me.u.enumeration.name_length);
        {
            struct gremlinp_parser_buffer eb = make_body(&buf, me.u.enumeration.body_start, me.u.enumeration.body_end);
            struct gremlinp_enum_entry_result ef;

            /* option allow_alias = true */
            ef = gremlinp_enum_next_entry(&eb);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_EQ(GREMLINP_ENUM_ENTRY_OPTION, ef.kind);
            ASSERT_SPAN_EQ("allow_alias", ef.u.option.name_start, ef.u.option.name_length);

            /* ALIAS_FOO = 0 */
            ef = gremlinp_enum_next_entry(&eb);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_SPAN_EQ("ALIAS_FOO", ef.u.field.name_start, ef.u.field.name_length);
            ASSERT_EQ(0, ef.u.field.index);

            /* ALIAS_BAR = 1 */
            ef = gremlinp_enum_next_entry(&eb);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_SPAN_EQ("ALIAS_BAR", ef.u.field.name_start, ef.u.field.name_length);
            ASSERT_EQ(1, ef.u.field.index);

            /* ALIAS_BAZ = 2 */
            ef = gremlinp_enum_next_entry(&eb);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_SPAN_EQ("ALIAS_BAZ", ef.u.field.name_start, ef.u.field.name_length);
            ASSERT_EQ(2, ef.u.field.index);

            /* MOO = 2 */
            ef = gremlinp_enum_next_entry(&eb);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_SPAN_EQ("MOO", ef.u.field.name_start, ef.u.field.name_length);
            ASSERT_EQ(2, ef.u.field.index);

            /* moo = 2 */
            ef = gremlinp_enum_next_entry(&eb);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_SPAN_EQ("moo", ef.u.field.name_start, ef.u.field.name_length);
            ASSERT_EQ(2, ef.u.field.index);

            /* bAz = 2 */
            ef = gremlinp_enum_next_entry(&eb);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_SPAN_EQ("bAz", ef.u.field.name_start, ef.u.field.name_length);
            ASSERT_EQ(2, ef.u.field.index);
        }

        /* Singular fields: optional_int32 .. optional_bytes (15 fields) */
        struct { const char *name; int32_t idx; } singular[] = {
            {"optional_int32", 1},  {"optional_int64", 2},
            {"optional_uint32", 3}, {"optional_uint64", 4},
            {"optional_sint32", 5}, {"optional_sint64", 6},
            {"optional_fixed32", 7}, {"optional_fixed64", 8},
            {"optional_sfixed32", 9}, {"optional_sfixed64", 10},
            {"optional_float", 11}, {"optional_double", 12},
            {"optional_bool", 13},  {"optional_string", 14},
            {"optional_bytes", 15}
        };
        size_t si;
        for (si = 0; si < sizeof(singular) / sizeof(singular[0]); si++) {
            me = gremlinp_message_next_entry(&body);
            ASSERT_EQ(GREMLINP_OK, me.error);
            ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
            ASSERT_EQ(GREMLINP_FIELD_LABEL_NONE, me.u.field.label);
            ASSERT_SPAN_EQ(singular[si].name, me.u.field.name_start, me.u.field.name_length);
            ASSERT_EQ(singular[si].idx, me.u.field.index);
        }

        /* optional_nested_message=18, optional_foreign_message=19 */
        me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
        ASSERT_SPAN_EQ("optional_nested_message", me.u.field.name_start, me.u.field.name_length);
        ASSERT_EQ(18, me.u.field.index);

        me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
        ASSERT_SPAN_EQ("optional_foreign_message", me.u.field.name_start, me.u.field.name_length);
        ASSERT_EQ(19, me.u.field.index);

        /* optional_nested_enum=21, optional_foreign_enum=22, optional_aliased_enum=23 */
        struct { const char *name; int32_t idx; } enum_fields[] = {
            {"optional_nested_enum", 21},
            {"optional_foreign_enum", 22},
            {"optional_aliased_enum", 23}
        };
        size_t ei;
        for (ei = 0; ei < 3; ei++) {
            me = gremlinp_message_next_entry(&body);
            ASSERT_EQ(GREMLINP_OK, me.error);
            ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
            ASSERT_SPAN_EQ(enum_fields[ei].name, me.u.field.name_start, me.u.field.name_length);
            ASSERT_EQ(enum_fields[ei].idx, me.u.field.index);
        }

        /* optional_string_piece=24 [ctype=STRING_PIECE] */
        me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
        ASSERT_SPAN_EQ("optional_string_piece", me.u.field.name_start, me.u.field.name_length);
        ASSERT_EQ(24, me.u.field.index);
        ASSERT_EQ(1, me.u.field.options.count);

        /* optional_cord=25 [ctype=CORD] */
        me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
        ASSERT_SPAN_EQ("optional_cord", me.u.field.name_start, me.u.field.name_length);
        ASSERT_EQ(25, me.u.field.index);
        ASSERT_EQ(1, me.u.field.options.count);

        /* recursive_message=27 */
        me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
        ASSERT_SPAN_EQ("recursive_message", me.u.field.name_start, me.u.field.name_length);
        ASSERT_EQ(27, me.u.field.index);

        /* Repeated fields: 31..45, 48..49, 51..52 */
        struct { const char *name; int32_t idx; } repeated[] = {
            {"repeated_int32", 31},   {"repeated_int64", 32},
            {"repeated_uint32", 33},  {"repeated_uint64", 34},
            {"repeated_sint32", 35},  {"repeated_sint64", 36},
            {"repeated_fixed32", 37}, {"repeated_fixed64", 38},
            {"repeated_sfixed32", 39},{"repeated_sfixed64", 40},
            {"repeated_float", 41},   {"repeated_double", 42},
            {"repeated_bool", 43},    {"repeated_string", 44},
            {"repeated_bytes", 45},
            {"repeated_nested_message", 48},
            {"repeated_foreign_message", 49},
            {"repeated_nested_enum", 51},
            {"repeated_foreign_enum", 52}
        };
        size_t ri;
        for (ri = 0; ri < sizeof(repeated) / sizeof(repeated[0]); ri++) {
            me = gremlinp_message_next_entry(&body);
            ASSERT_EQ(GREMLINP_OK, me.error);
            ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
            ASSERT_EQ(GREMLINP_FIELD_LABEL_REPEATED, me.u.field.label);
            ASSERT_SPAN_EQ(repeated[ri].name, me.u.field.name_start, me.u.field.name_length);
            ASSERT_EQ(repeated[ri].idx, me.u.field.index);
        }

        /* repeated_string_piece=54 [ctype=STRING_PIECE] */
        me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
        ASSERT_EQ(GREMLINP_FIELD_LABEL_REPEATED, me.u.field.label);
        ASSERT_SPAN_EQ("repeated_string_piece", me.u.field.name_start, me.u.field.name_length);
        ASSERT_EQ(54, me.u.field.index);
        ASSERT_EQ(1, me.u.field.options.count);

        /* repeated_cord=55 [ctype=CORD] */
        me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
        ASSERT_EQ(GREMLINP_FIELD_LABEL_REPEATED, me.u.field.label);
        ASSERT_SPAN_EQ("repeated_cord", me.u.field.name_start, me.u.field.name_length);
        ASSERT_EQ(55, me.u.field.index);
        ASSERT_EQ(1, me.u.field.options.count);

        /* Packed: 75..88 */
        struct { const char *name; int32_t idx; } packed[] = {
            {"packed_int32", 75},   {"packed_int64", 76},
            {"packed_uint32", 77},  {"packed_uint64", 78},
            {"packed_sint32", 79},  {"packed_sint64", 80},
            {"packed_fixed32", 81}, {"packed_fixed64", 82},
            {"packed_sfixed32", 83},{"packed_sfixed64", 84},
            {"packed_float", 85},   {"packed_double", 86},
            {"packed_bool", 87},    {"packed_nested_enum", 88}
        };
        size_t pi;
        for (pi = 0; pi < sizeof(packed) / sizeof(packed[0]); pi++) {
            me = gremlinp_message_next_entry(&body);
            ASSERT_EQ(GREMLINP_OK, me.error);
            ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
            ASSERT_EQ(GREMLINP_FIELD_LABEL_REPEATED, me.u.field.label);
            ASSERT_SPAN_EQ(packed[pi].name, me.u.field.name_start, me.u.field.name_length);
            ASSERT_EQ(packed[pi].idx, me.u.field.index);
        }

        /* Unpacked: 89..102 (14 fields, each with [features.repeated_field_encoding = EXPANDED]) */
        struct { const char *name; int32_t idx; } unpacked[] = {
            {"unpacked_int32", 89},   {"unpacked_int64", 90},
            {"unpacked_uint32", 91},  {"unpacked_uint64", 92},
            {"unpacked_sint32", 93},  {"unpacked_sint64", 94},
            {"unpacked_fixed32", 95}, {"unpacked_fixed64", 96},
            {"unpacked_sfixed32", 97},{"unpacked_sfixed64", 98},
            {"unpacked_float", 99},   {"unpacked_double", 100},
            {"unpacked_bool", 101},   {"unpacked_nested_enum", 102}
        };
        size_t ui;
        for (ui = 0; ui < sizeof(unpacked) / sizeof(unpacked[0]); ui++) {
            me = gremlinp_message_next_entry(&body);
            ASSERT_EQ(GREMLINP_OK, me.error);
            ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
            ASSERT_EQ(GREMLINP_FIELD_LABEL_REPEATED, me.u.field.label);
            ASSERT_SPAN_EQ(unpacked[ui].name, me.u.field.name_start, me.u.field.name_length);
            ASSERT_EQ(unpacked[ui].idx, me.u.field.index);
            ASSERT_EQ(1, me.u.field.options.count);
        }

        /* Map fields: 56..74 (19 maps) */
        struct { const char *name; const char *key; const char *val; int32_t idx; } maps[] = {
            {"map_int32_int32",           "int32",   "int32",          56},
            {"map_int64_int64",           "int64",   "int64",          57},
            {"map_uint32_uint32",         "uint32",  "uint32",         58},
            {"map_uint64_uint64",         "uint64",  "uint64",         59},
            {"map_sint32_sint32",         "sint32",  "sint32",         60},
            {"map_sint64_sint64",         "sint64",  "sint64",         61},
            {"map_fixed32_fixed32",       "fixed32", "fixed32",        62},
            {"map_fixed64_fixed64",       "fixed64", "fixed64",        63},
            {"map_sfixed32_sfixed32",     "sfixed32","sfixed32",       64},
            {"map_sfixed64_sfixed64",     "sfixed64","sfixed64",       65},
            {"map_int32_float",           "int32",   "float",          66},
            {"map_int32_double",          "int32",   "double",         67},
            {"map_bool_bool",             "bool",    "bool",           68},
            {"map_string_string",         "string",  "string",         69},
            {"map_string_bytes",          "string",  "bytes",          70},
            {"map_string_nested_message", "string",  "NestedMessage",  71},
            {"map_string_foreign_message","string",  "ForeignMessage", 72},
            {"map_string_nested_enum",    "string",  "NestedEnum",     73},
            {"map_string_foreign_enum",   "string",  "ForeignEnum",    74}
        };
        size_t mi;
        for (mi = 0; mi < sizeof(maps) / sizeof(maps[0]); mi++) {
            me = gremlinp_message_next_entry(&body);
            ASSERT_EQ(GREMLINP_OK, me.error);
            ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
            ASSERT_EQ(GREMLINP_FIELD_TYPE_MAP, me.u.field.type.kind);
            ASSERT_SPAN_EQ(maps[mi].name, me.u.field.name_start, me.u.field.name_length);
            ASSERT_EQ(maps[mi].idx, me.u.field.index);
            ASSERT_SPAN_EQ(maps[mi].key, me.u.field.type.u.map.key_type.start, me.u.field.type.u.map.key_type.length);
            ASSERT_SPAN_EQ(maps[mi].val, me.u.field.type.u.map.value_type.start, me.u.field.type.u.map.value_type.length);
        }

        /* oneof oneof_field { ... } */
        me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_ONEOF, me.kind);
        ASSERT_SPAN_EQ("oneof_field", me.u.oneof.name_start, me.u.oneof.name_length);
        {
            struct gremlinp_parser_buffer ob = make_body(&buf, me.u.oneof.body_start, me.u.oneof.body_end);
            struct { const char *type; const char *name; int32_t idx; } oneof_fields[] = {
                {"uint32",                    "oneof_uint32",         111},
                {"NestedMessage",             "oneof_nested_message", 112},
                {"string",                    "oneof_string",         113},
                {"bytes",                     "oneof_bytes",          114},
                {"bool",                      "oneof_bool",           115},
                {"uint64",                    "oneof_uint64",         116},
                {"float",                     "oneof_float",          117},
                {"double",                    "oneof_double",         118},
                {"NestedEnum",                "oneof_enum",           119},
                {"google.protobuf.NullValue", "oneof_null_value",     120}
            };
            size_t oi;
            for (oi = 0; oi < 10; oi++) {
                struct gremlinp_oneof_entry_result oe = gremlinp_oneof_next_entry(&ob);
                ASSERT_EQ(GREMLINP_OK, oe.error);
                ASSERT_EQ(GREMLINP_ONEOF_ENTRY_FIELD, oe.kind);
                ASSERT_SPAN_EQ(oneof_fields[oi].type, oe.u.field.type_name.start, oe.u.field.type_name.length);
                ASSERT_SPAN_EQ(oneof_fields[oi].name, oe.u.field.name_start, oe.u.field.name_length);
                ASSERT_EQ(oneof_fields[oi].idx, oe.u.field.index);
            }
        }

        /* Well-known type fields (201..209, 211..219, 301..307, 311..317) */
        struct { const char *name; int32_t idx; enum gremlinp_field_label label; } wkt[] = {
            {"optional_bool_wrapper", 201, GREMLINP_FIELD_LABEL_NONE},
            {"optional_int32_wrapper", 202, GREMLINP_FIELD_LABEL_NONE},
            {"optional_int64_wrapper", 203, GREMLINP_FIELD_LABEL_NONE},
            {"optional_uint32_wrapper", 204, GREMLINP_FIELD_LABEL_NONE},
            {"optional_uint64_wrapper", 205, GREMLINP_FIELD_LABEL_NONE},
            {"optional_float_wrapper", 206, GREMLINP_FIELD_LABEL_NONE},
            {"optional_double_wrapper", 207, GREMLINP_FIELD_LABEL_NONE},
            {"optional_string_wrapper", 208, GREMLINP_FIELD_LABEL_NONE},
            {"optional_bytes_wrapper", 209, GREMLINP_FIELD_LABEL_NONE},
            {"repeated_bool_wrapper", 211, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_int32_wrapper", 212, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_int64_wrapper", 213, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_uint32_wrapper", 214, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_uint64_wrapper", 215, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_float_wrapper", 216, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_double_wrapper", 217, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_string_wrapper", 218, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_bytes_wrapper", 219, GREMLINP_FIELD_LABEL_REPEATED},
            {"optional_duration", 301, GREMLINP_FIELD_LABEL_NONE},
            {"optional_timestamp", 302, GREMLINP_FIELD_LABEL_NONE},
            {"optional_field_mask", 303, GREMLINP_FIELD_LABEL_NONE},
            {"optional_struct", 304, GREMLINP_FIELD_LABEL_NONE},
            {"optional_any", 305, GREMLINP_FIELD_LABEL_NONE},
            {"optional_value", 306, GREMLINP_FIELD_LABEL_NONE},
            {"optional_null_value", 307, GREMLINP_FIELD_LABEL_NONE},
            {"repeated_duration", 311, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_timestamp", 312, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_fieldmask", 313, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_struct", 324, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_any", 315, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_value", 316, GREMLINP_FIELD_LABEL_REPEATED},
            {"repeated_list_value", 317, GREMLINP_FIELD_LABEL_REPEATED}
        };
        size_t wi;
        for (wi = 0; wi < sizeof(wkt) / sizeof(wkt[0]); wi++) {
            me = gremlinp_message_next_entry(&body);
            ASSERT_EQ(GREMLINP_OK, me.error);
            ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
            ASSERT_EQ(wkt[wi].label, me.u.field.label);
            ASSERT_SPAN_EQ(wkt[wi].name, me.u.field.name_start, me.u.field.name_length);
            ASSERT_EQ(wkt[wi].idx, me.u.field.index);
        }

        /* JSON name fields: fieldname1(401)..Field_name18__(418) */
        struct { const char *name; int32_t idx; } json_names[] = {
            {"fieldname1", 401},     {"field_name2", 402},
            {"_field_name3", 403},   {"field__name4_", 404},
            {"field0name5", 405},    {"field_0_name6", 406},
            {"fieldName7", 407},     {"FieldName8", 408},
            {"field_Name9", 409},    {"Field_Name10", 410},
            {"FIELD_NAME11", 411},   {"FIELD_name12", 412},
            {"__field_name13", 413}, {"__Field_name14", 414},
            {"field__name15", 415},  {"field__Name16", 416},
            {"field_name17__", 417}, {"Field_name18__", 418}
        };
        size_t ji;
        for (ji = 0; ji < sizeof(json_names) / sizeof(json_names[0]); ji++) {
            me = gremlinp_message_next_entry(&body);
            ASSERT_EQ(GREMLINP_OK, me.error);
            ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
            ASSERT_SPAN_EQ(json_names[ji].name, me.u.field.name_start, me.u.field.name_length);
            ASSERT_EQ(json_names[ji].idx, me.u.field.index);
        }

        /* reserved 501 to 510 */
        me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_RESERVED, me.kind);

        /* body done */
        gremlinp_parser_buffer_skip_spaces(&body);
        ASSERT_EQ(body.offset, body.buf_size);
    }

    /* ---- message ForeignMessage ---- */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_MESSAGE, fe.kind);
    ASSERT_SPAN_EQ("ForeignMessage", fe.u.message.name_start, fe.u.message.name_length);
    {
        struct gremlinp_parser_buffer body = make_body(&buf, fe.u.message.body_start, fe.u.message.body_end);
        struct gremlinp_message_entry_result me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_FIELD, me.kind);
        ASSERT_SPAN_EQ("c", me.u.field.name_start, me.u.field.name_length);
        ASSERT_EQ(1, me.u.field.index);
    }

    /* ---- enum ForeignEnum ---- */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_ENUM, fe.kind);
    ASSERT_SPAN_EQ("ForeignEnum", fe.u.enumeration.name_start, fe.u.enumeration.name_length);
    {
        struct gremlinp_parser_buffer body = make_body(&buf, fe.u.enumeration.body_start, fe.u.enumeration.body_end);
        struct { const char *name; int32_t idx; } foreign_vals[] = {
            {"FOREIGN_FOO", 0}, {"FOREIGN_BAR", 1}, {"FOREIGN_BAZ", 2}
        };
        size_t fi;
        for (fi = 0; fi < 3; fi++) {
            struct gremlinp_enum_entry_result ef = gremlinp_enum_next_entry(&body);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_EQ(GREMLINP_ENUM_ENTRY_FIELD, ef.kind);
            ASSERT_SPAN_EQ(foreign_vals[fi].name, ef.u.field.name_start, ef.u.field.name_length);
            ASSERT_EQ(foreign_vals[fi].idx, ef.u.field.index);
        }
    }

    /* ---- message NullHypothesisProto3 (empty) ---- */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_MESSAGE, fe.kind);
    ASSERT_SPAN_EQ("NullHypothesisProto3", fe.u.message.name_start, fe.u.message.name_length);

    /* ---- message EnumOnlyProto3 (contains enum Bool) ---- */
    fe = gremlinp_file_next_entry(&buf);
    ASSERT_EQ(GREMLINP_OK, fe.error);
    ASSERT_EQ(GREMLINP_FILE_ENTRY_MESSAGE, fe.kind);
    ASSERT_SPAN_EQ("EnumOnlyProto3", fe.u.message.name_start, fe.u.message.name_length);
    {
        struct gremlinp_parser_buffer body = make_body(&buf, fe.u.message.body_start, fe.u.message.body_end);
        struct gremlinp_message_entry_result me = gremlinp_message_next_entry(&body);
        ASSERT_EQ(GREMLINP_OK, me.error);
        ASSERT_EQ(GREMLINP_MSG_ENTRY_ENUM, me.kind);
        ASSERT_SPAN_EQ("Bool", me.u.enumeration.name_start, me.u.enumeration.name_length);
        {
            struct gremlinp_parser_buffer eb = make_body(&buf, me.u.enumeration.body_start, me.u.enumeration.body_end);

            struct gremlinp_enum_entry_result ef = gremlinp_enum_next_entry(&eb);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_SPAN_EQ("kFalse", ef.u.field.name_start, ef.u.field.name_length);
            ASSERT_EQ(0, ef.u.field.index);

            ef = gremlinp_enum_next_entry(&eb);
            ASSERT_EQ(GREMLINP_OK, ef.error);
            ASSERT_SPAN_EQ("kTrue", ef.u.field.name_start, ef.u.field.name_length);
            ASSERT_EQ(1, ef.u.field.index);
        }
    }

    /* EOF */
    gremlinp_parser_buffer_skip_spaces(&buf);
    ASSERT_EQ('\0', gremlinp_parser_buffer_char(&buf));

    free(content);
}

/* ======================================================================== */

void
integration_test(void)
{
    RUN_TEST(integration_test_full_proto3);
    RUN_TEST(integration_test_enum_detailed);
    RUN_TEST(integration_test_message_all_fields);
    RUN_TEST(integration_test_nested_structures);
    RUN_TEST(integration_test_oneof_iteration);
    RUN_TEST(integration_test_map_fields);
    RUN_TEST(integration_test_file_entry_iterator);
    RUN_TEST(integration_test_message_entry_iterator);
    RUN_TEST(integration_test_edition);
    RUN_TEST(integration_test_golden_proto3);
}

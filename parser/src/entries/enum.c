#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

static const char KW_ENUM[] = "enum";

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.name_length > 0 &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset &&
               buf->buf[buf->offset - 1] == '}';
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_enum_parse_result
gremlinp_enum_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_enum_parse_result result = {NULL, 0, 0, 0, 0, 0, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_ENUM, sizeof(KW_ENUM) - 1)) {
        result.error = GREMLINP_ERROR_UNEXPECTED_TOKEN;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    struct gremlinp_span_parse_result name = gremlinp_lexems_parse_identifier(buf);
    if (name.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = name.error;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    if (!gremlinp_parser_buffer_check_and_shift(buf, '{')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_BRACKET_EXPECTED;
        return result;
    }

    size_t body_start = buf->offset;

    // Parse body: fields, options, reserved, empty semicolons
    /*@ loop invariant buf->offset >= body_start;
        loop invariant buf->offset <= buf->buf_size;
        loop assigns buf->offset, errno, err;
        loop variant buf->buf_size - buf->offset;
    */
    while (true) {
        err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

        char c = gremlinp_parser_buffer_char(buf);

        if (c == '}') {
            break;
        }

        if (c == '\0') {
            buf->offset = start;
            result.error = GREMLINP_ERROR_UNEXPECTED_EOF;
            return result;
        }

        // Skip empty semicolons
        if (c == ';') {
            buf->offset++;
            continue;
        }

        // Try option
        struct gremlinp_option_parse_result opt = gremlinp_option_parse(buf);
        if (opt.error == GREMLINP_OK) {
            continue;
        }

        // Try reserved
        struct gremlinp_reserved_parse_result res = gremlinp_reserved_parse(buf);
        if (res.error == GREMLINP_OK) {
            continue;
        }

        // Try enum field
        struct gremlinp_enum_field_parse_result field = gremlinp_enum_field_parse(buf);
        if (field.error == GREMLINP_OK) {
            continue;
        }

        // Nothing matched
        buf->offset = start;
        result.error = GREMLINP_ERROR_INVALID_ENUM_DEF;
        return result;
    }

    size_t body_end = buf->offset;
    buf->offset++; // consume '}'

    result.name_start = name.start;
    result.name_length = name.length;
    result.body_start = body_start;
    result.body_end = body_end;
    result.start = start;
    result.end = buf->offset;
    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start >= \old(buf->offset) &&
               \result.end == buf->offset &&
               (\result.kind == GREMLINP_ENUM_ENTRY_FIELD ||
                \result.kind == GREMLINP_ENUM_ENTRY_OPTION ||
                \result.kind == GREMLINP_ENUM_ENTRY_RESERVED);
    ensures  \result.error == GREMLINP_OK ==>
               (\result.kind == GREMLINP_ENUM_ENTRY_FIELD    ==> \result.u.field.error    == GREMLINP_OK) &&
               (\result.kind == GREMLINP_ENUM_ENTRY_OPTION   ==> \result.u.option.error   == GREMLINP_OK) &&
               (\result.kind == GREMLINP_ENUM_ENTRY_RESERVED ==> \result.u.reserved.error == GREMLINP_OK);
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_enum_entry_result
gremlinp_enum_next_entry(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_enum_entry_result result;
    result.kind = GREMLINP_ENUM_ENTRY_FIELD;
    result.start = 0;
    result.end = 0;
    result.error = GREMLINP_OK;

    size_t saved = buf->offset;

    gremlinp_parser_buffer_skip_spaces(buf);

    // Skip empty semicolons
    /*@ loop invariant buf->offset >= \at(buf->offset, Pre);
        loop invariant buf->offset <= buf->buf_size;
        loop assigns buf->offset;
        loop variant buf->buf_size - buf->offset;
    */
    while (gremlinp_parser_buffer_char(buf) == ';') {
        buf->offset++;
        gremlinp_parser_buffer_skip_spaces(buf);
    }

    size_t entry_start = buf->offset;
    /*@ assert \separated(&saved, &entry_start, buf, &errno); */

    struct gremlinp_option_parse_result opt = gremlinp_option_parse(buf);
    if (opt.error == GREMLINP_OK) {
        result.kind = GREMLINP_ENUM_ENTRY_OPTION;
        result.u.option = opt;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_reserved_parse_result res = gremlinp_reserved_parse(buf);
    if (res.error == GREMLINP_OK) {
        result.kind = GREMLINP_ENUM_ENTRY_RESERVED;
        result.u.reserved = res;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_enum_field_parse_result field = gremlinp_enum_field_parse(buf);
    if (field.error == GREMLINP_OK) {
        result.kind = GREMLINP_ENUM_ENTRY_FIELD;
        result.u.field = field;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    buf->offset = saved;
    result.error = field.error;
    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.name_length > 0 &&
               buf->buf[buf->offset - 1] == ';' &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset;
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_enum_field_parse_result
gremlinp_enum_field_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_option_list_result empty_opts = {0, 0, 0, GREMLINP_OK};
    struct gremlinp_enum_field_parse_result result = {NULL, 0, 0, empty_opts, 0, 0, GREMLINP_OK};

    size_t field_start = buf->offset;

    struct gremlinp_span_parse_result name = gremlinp_lexems_parse_identifier(buf);
    if (name.error != GREMLINP_OK) {
        result.error = name.error;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = field_start; result.error = err; return result; }

    if (!gremlinp_parser_buffer_check_and_shift(buf, '=')) {
        buf->offset = field_start;
        result.error = GREMLINP_ERROR_ASSIGNMENT_EXPECTED;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = field_start; result.error = err; return result; }

    struct gremlinp_int32_parse_result num = gremlinp_lexems_parse_enum_value_number(buf);
    if (num.error != GREMLINP_OK) {
        buf->offset = field_start;
        result.error = num.error;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = field_start; result.error = err; return result; }

    if (gremlinp_parser_buffer_char(buf) == '[') {
        struct gremlinp_option_list_result opts = gremlinp_option_list_consume(buf);
        if (opts.error != GREMLINP_OK) {
            buf->offset = field_start;
            result.error = opts.error;
            return result;
        }
        result.options = opts;

        err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = field_start; result.error = err; return result; }
    }

    if (!gremlinp_parser_buffer_check_and_shift(buf, ';')) {
        buf->offset = field_start;
        result.error = GREMLINP_ERROR_SEMICOLON_EXPECTED;
        return result;
    }

    result.name_start = name.start;
    result.name_length = name.length;
    result.index = num.value;
    result.start = field_start;
    result.end = buf->offset;
    return result;
}

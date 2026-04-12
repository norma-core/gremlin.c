#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

static const char KW_ONEOF[] = "oneof";
/*@ axiomatic Kw_oneof_nonempty { axiom kw_oneof_nonempty: KW_ONEOF[0] == 'o'; } */

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
struct gremlinp_oneof_field_parse_result
gremlinp_oneof_field_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_span_parse_result empty_span = {NULL, 0, GREMLINP_OK};
    struct gremlinp_option_list_result empty_opts = {0, 0, 0, GREMLINP_OK};
    struct gremlinp_oneof_field_parse_result result = {
        empty_span, NULL, 0, 0, empty_opts, 0, 0, GREMLINP_OK
    };

    size_t start = buf->offset;

    // Type (named only, no map in oneof)
    struct gremlinp_span_parse_result type_name = gremlinp_lexems_parse_full_identifier(buf);
    if (type_name.error != GREMLINP_OK) {
        result.error = type_name.error;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    // Name
    struct gremlinp_span_parse_result name = gremlinp_lexems_parse_identifier(buf);
    if (name.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = name.error;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    // = number
    if (!gremlinp_parser_buffer_check_and_shift(buf, '=')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_ASSIGNMENT_EXPECTED;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    struct gremlinp_int32_parse_result num = gremlinp_lexems_parse_field_number(buf);
    if (num.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = num.error;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    // Optional [options]
    if (gremlinp_parser_buffer_char(buf) == '[') {
        struct gremlinp_option_list_result opts = gremlinp_option_list_consume(buf);
        if (opts.error != GREMLINP_OK) {
            buf->offset = start;
            result.error = opts.error;
            return result;
        }
        result.options = opts;

        err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }
    }

    // ;
    if (!gremlinp_parser_buffer_check_and_shift(buf, ';')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_SEMICOLON_EXPECTED;
        return result;
    }

    result.type_name = type_name;
    result.name_start = name.start;
    result.name_length = name.length;
    result.index = num.value;
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
               (\result.kind == GREMLINP_ONEOF_ENTRY_FIELD ||
                \result.kind == GREMLINP_ONEOF_ENTRY_OPTION);
    ensures  \result.error == GREMLINP_OK ==>
               (\result.kind == GREMLINP_ONEOF_ENTRY_FIELD  ==> \result.u.field.error  == GREMLINP_OK) &&
               (\result.kind == GREMLINP_ONEOF_ENTRY_OPTION ==> \result.u.option.error == GREMLINP_OK);
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_oneof_entry_result
gremlinp_oneof_next_entry(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_oneof_entry_result result;
    result.kind = GREMLINP_ONEOF_ENTRY_FIELD;
    result.start = 0;
    result.end = 0;
    result.error = GREMLINP_OK;

    size_t saved = buf->offset;

    gremlinp_parser_buffer_skip_spaces(buf);

    // Skip semicolons
    /*@ loop invariant buf->offset >= saved;
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
        result.kind = GREMLINP_ONEOF_ENTRY_OPTION;
        result.u.option = opt;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_oneof_field_parse_result field = gremlinp_oneof_field_parse(buf);
    if (field.error == GREMLINP_OK) {
        result.kind = GREMLINP_ONEOF_ENTRY_FIELD;
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
               buf->buf[buf->offset - 1] == '}' &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset;
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_oneof_parse_result
gremlinp_oneof_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_oneof_parse_result result = {NULL, 0, 0, 0, 0, 0, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_ONEOF)) {
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

    // Parse body: oneof fields and options
    /*@ loop invariant buf->offset >= body_start;
        loop invariant buf->offset <= buf->buf_size;
        loop assigns buf->offset, errno, err;
        loop variant buf->buf_size - buf->offset;
    */
    while (true) {
        err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

        char c = gremlinp_parser_buffer_char(buf);

        if (c == '}') break;

        if (c == '\0') {
            buf->offset = start;
            result.error = GREMLINP_ERROR_UNEXPECTED_EOF;
            return result;
        }

        if (c == ';') {
            buf->offset++;
            continue;
        }

        // Try option
        struct gremlinp_option_parse_result opt = gremlinp_option_parse(buf);
        if (opt.error == GREMLINP_OK) continue;

        // Try oneof field
        struct gremlinp_oneof_field_parse_result field = gremlinp_oneof_field_parse(buf);
        if (field.error == GREMLINP_OK) continue;

        buf->offset = start;
        result.error = GREMLINP_ERROR_INVALID_ONEOF_ELEMENT;
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

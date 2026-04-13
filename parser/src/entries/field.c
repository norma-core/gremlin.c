#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

static const char KW_OPTIONAL[] = "optional";
static const char KW_REQUIRED[] = "required";
static const char KW_REPEATED[] = "repeated";

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.name_length > 0 &&
               buf->buf[buf->offset - 1] == ';' &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset &&
               (\result.label == GREMLINP_FIELD_LABEL_NONE ||
                \result.label == GREMLINP_FIELD_LABEL_OPTIONAL ||
                \result.label == GREMLINP_FIELD_LABEL_REQUIRED ||
                \result.label == GREMLINP_FIELD_LABEL_REPEATED);
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_field_parse_result
gremlinp_field_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_option_list_result empty_opts = {0, 0, 0, GREMLINP_OK};
    struct gremlinp_span_parse_result empty_span = {NULL, 0, GREMLINP_OK};
    struct gremlinp_field_type_parse_result empty_type;
    empty_type.u.named = empty_span;
    empty_type.kind = GREMLINP_FIELD_TYPE_NAMED;
    empty_type.start = 0;
    empty_type.end = 0;
    empty_type.error = GREMLINP_OK;

    struct gremlinp_field_parse_result result = {
        GREMLINP_FIELD_LABEL_NONE, empty_type,
        NULL, 0, 0, empty_opts, 0, 0, GREMLINP_OK
    };

    size_t start = buf->offset;

    // Optional label
    if (gremlinp_parser_buffer_check_str_and_shift(buf, KW_OPTIONAL, sizeof(KW_OPTIONAL) - 1)) {
        result.label = GREMLINP_FIELD_LABEL_OPTIONAL;
        enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }
    } else if (gremlinp_parser_buffer_check_str_and_shift(buf, KW_REQUIRED, sizeof(KW_REQUIRED) - 1)) {
        result.label = GREMLINP_FIELD_LABEL_REQUIRED;
        enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }
    } else if (gremlinp_parser_buffer_check_str_and_shift(buf, KW_REPEATED, sizeof(KW_REPEATED) - 1)) {
        result.label = GREMLINP_FIELD_LABEL_REPEATED;
        enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }
    }

    // Type
    struct gremlinp_field_type_parse_result type = gremlinp_field_type_parse(buf);
    if (type.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = type.error;
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

    result.type = type;
    result.name_start = name.start;
    result.name_length = name.length;
    result.index = num.value;
    result.start = start;
    result.end = buf->offset;
    return result;
}

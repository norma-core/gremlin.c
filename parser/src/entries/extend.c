#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

static const char KW_EXTEND[] = "extend";
/*@ axiomatic Kw_extend_nonempty { axiom kw_extend_nonempty: KW_EXTEND[0] == 'e'; } */

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset &&
               buf->buf[buf->offset - 1] == '}' &&
               \result.base_type.length > 0;
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_extend_parse_result
gremlinp_extend_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_span_parse_result empty_span = {NULL, 0, GREMLINP_OK};
    struct gremlinp_extend_parse_result result = {empty_span, 0, 0, 0, 0, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_EXTEND)) {
        result.error = GREMLINP_ERROR_UNEXPECTED_TOKEN;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    struct gremlinp_span_parse_result base = gremlinp_lexems_parse_full_identifier(buf);
    if (base.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = base.error;
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

    // Parse body: fields (reuse gremlinp_field_parse)
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

        struct gremlinp_field_parse_result field = gremlinp_field_parse(buf);
        if (field.error == GREMLINP_OK) continue;

        buf->offset = start;
        result.error = field.error;
        return result;
    }

    size_t body_end = buf->offset;
    buf->offset++;

    result.base_type = base;
    result.body_start = body_start;
    result.body_end = body_end;
    result.start = start;
    result.end = buf->offset;
    return result;
}

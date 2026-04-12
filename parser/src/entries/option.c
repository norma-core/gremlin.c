#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

static const char KW_OPTION[] = "option";
/*@ axiomatic Kw_option_nonempty { axiom kw_option_nonempty: KW_OPTION[0] == 'o'; } */

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset);
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
static struct gremlinp_span_parse_result
parse_option_name_part(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_span_parse_result result = {NULL, 0, GREMLINP_OK};
    size_t start = buf->offset;

    char c = gremlinp_parser_buffer_char(buf);

    if (c == '(') {
        buf->offset++;
        if (gremlinp_parser_buffer_char(buf) == '.') {
            buf->offset++;
        }

        struct gremlinp_span_parse_result ident = gremlinp_lexems_parse_full_identifier(buf);
        if (ident.error != GREMLINP_OK) {
            buf->offset = start;
            result.error = GREMLINP_ERROR_INVALID_OPTION_NAME;
            return result;
        }

        if (!gremlinp_parser_buffer_check_and_shift(buf, ')')) {
            buf->offset = start;
            result.error = GREMLINP_ERROR_INVALID_OPTION_NAME;
            return result;
        }
    } else {
        struct gremlinp_span_parse_result ident = gremlinp_lexems_parse_identifier(buf);
        if (ident.error != GREMLINP_OK) {
            result.error = GREMLINP_ERROR_INVALID_OPTION_NAME;
            return result;
        }
    }

    result.start = buf->buf + start;
    result.length = buf->offset - start;
    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.name_length > 0;
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_option_parse_result
gremlinp_option_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_const_parse_result empty_val;
    empty_val.error = GREMLINP_OK;
    struct gremlinp_option_parse_result result = {NULL, 0, empty_val, 0, 0, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_OPTION)) {
        result.error = GREMLINP_ERROR_UNEXPECTED_TOKEN;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    // Parse option name (may contain dots and parenthesized extensions)
    size_t name_start = buf->offset;

    struct gremlinp_span_parse_result first_part = parse_option_name_part(buf);
    if (first_part.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = first_part.error;
        return result;
    }

    /*@ loop invariant buf->offset <= buf->buf_size;
        loop invariant buf->offset >= \at(buf->offset, LoopEntry);
        loop assigns buf->offset;
        loop variant buf->buf_size - buf->offset;
    */
    while (gremlinp_parser_buffer_char(buf) == '.') {
        buf->offset++;
        struct gremlinp_span_parse_result next_part = parse_option_name_part(buf);
        if (next_part.error != GREMLINP_OK) {
            buf->offset = start;
            result.error = next_part.error;
            return result;
        }
    }

    size_t name_end = buf->offset;

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    if (!gremlinp_parser_buffer_check_and_shift(buf, '=')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_ASSIGNMENT_EXPECTED;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    struct gremlinp_const_parse_result val = gremlinp_lexems_parse_const_value(buf);
    if (val.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = val.error;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    if (!gremlinp_parser_buffer_check_and_shift(buf, ';')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_SEMICOLON_EXPECTED;
        return result;
    }

    result.name_start = buf->buf + name_start;
    result.name_length = name_end - name_start;
    result.value = val;
    result.start = start;
    result.end = buf->offset;
    return result;
}

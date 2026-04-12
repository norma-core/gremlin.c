#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

static const char KW_RESERVED[] = "reserved";
/*@ axiomatic Kw_reserved_nonempty { axiom kw_reserved_nonempty: KW_RESERVED[0] == 'r'; } */

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset &&
               buf->buf[buf->offset - 1] == ';' &&
               \result.count >= 1 &&
               (\result.kind == GREMLINP_RESERVED_RANGES ||
                \result.kind == GREMLINP_RESERVED_NAMES);
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_reserved_parse_result
gremlinp_reserved_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_reserved_parse_result result = {0, 0, 0, GREMLINP_RESERVED_RANGES, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_RESERVED)) {
        result.error = GREMLINP_ERROR_UNEXPECTED_TOKEN;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    // Peek to determine kind: string literal means names, digit means ranges
    char first = gremlinp_parser_buffer_char(buf);
    bool is_names = (first == '"' || first == '\'');
    size_t count = 0;

    if (is_names) {
        result.kind = GREMLINP_RESERVED_NAMES;
        /*@ loop invariant buf->offset > start;
            loop invariant buf->offset <= buf->buf_size;
            loop invariant count >= 0;
            loop assigns buf->offset, count, err;
            loop variant buf->buf_size - buf->offset;
        */
        while (true) {
            struct gremlinp_span_parse_result name = gremlinp_lexems_parse_string_literal(buf);
            if (name.error != GREMLINP_OK) {
                buf->offset = start;
                result.error = name.error;
                return result;
            }
            count++;

            err = gremlinp_parser_buffer_skip_spaces(buf);
            if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

            if (!gremlinp_parser_buffer_check_and_shift(buf, ',')) {
                break;
            }

            err = gremlinp_parser_buffer_skip_spaces(buf);
            if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }
        }
    } else {
        result.kind = GREMLINP_RESERVED_RANGES;
        /*@ loop invariant buf->offset > start;
            loop invariant buf->offset <= buf->buf_size;
            loop invariant count >= 0;
            loop assigns buf->offset, count, errno, err;
            loop variant buf->buf_size - buf->offset;
        */
        while (true) {
            struct gremlinp_range_parse_result range = gremlinp_lexems_parse_range(buf);
            if (range.error != GREMLINP_OK) {
                buf->offset = start;
                result.error = range.error;
                return result;
            }
            count++;

            err = gremlinp_parser_buffer_skip_spaces(buf);
            if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

            if (!gremlinp_parser_buffer_check_and_shift(buf, ',')) {
                break;
            }

            err = gremlinp_parser_buffer_skip_spaces(buf);
            if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }
        }
    }

    if (!gremlinp_parser_buffer_check_and_shift(buf, ';')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_SEMICOLON_EXPECTED;
        return result;
    }

    result.start = start;
    result.end = buf->offset;
    result.count = count;
    return result;
}

#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

static const char KW_SERVICE[] = "service";

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset &&
               buf->buf[buf->offset - 1] == '}';
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_service_parse_result
gremlinp_service_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_service_parse_result result = {0, 0, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_SERVICE, sizeof(KW_SERVICE) - 1)) {
        result.error = GREMLINP_ERROR_UNEXPECTED_TOKEN;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    // Parse service name
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

    // Skip body — count braces, handle strings. depth is size_t to avoid
    // signed-overflow concerns; the loop invariant ties it to the number
    // of bytes consumed so it is also bounded.
    size_t depth = 1;
    /*@ loop invariant buf->offset >= start;
        loop invariant buf->offset <= buf->buf_size;
        loop invariant depth <= buf->offset - start + 1;
        loop invariant depth == 0 ==>
                       buf->offset > start && buf->buf[buf->offset - 1] == '}';
        loop assigns buf->offset, depth;
        loop variant buf->buf_size - buf->offset;
    */
    while (depth > 0) {
        size_t iter_start = buf->offset;
        (void)iter_start;
        char c = gremlinp_parser_buffer_char(buf);
        if (c == '\0') {
            buf->offset = start;
            result.error = GREMLINP_ERROR_UNEXPECTED_EOF;
            return result;
        }
        buf->offset++;
        /*@ assert buf->offset == iter_start + 1; */
        if (c == '{') depth++;
        else if (c == '}') depth--;
        else if (c == '"' || c == '\'') {
            /*@ loop invariant buf->offset >= iter_start + 1;
                loop invariant buf->offset <= buf->buf_size;
                loop assigns buf->offset;
                loop variant buf->buf_size - buf->offset;
            */
            while (true) {
                char sc = gremlinp_parser_buffer_char(buf);
                if (sc == '\0') {
                    buf->offset = start;
                    result.error = GREMLINP_ERROR_UNEXPECTED_EOF;
                    return result;
                }
                buf->offset++;
                if (sc == c) break;
                if (sc == '\\' && gremlinp_parser_buffer_char(buf) != '\0') {
                    buf->offset++;
                }
            }
        }
        /*@ assert buf->offset > iter_start; */
    }

    result.start = start;
    result.end = buf->offset;
    return result;
}

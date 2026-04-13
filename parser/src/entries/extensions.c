#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

static const char KW_EXTENSIONS[] = "extensions";

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset &&
               buf->buf[buf->offset - 1] == ';' &&
               \result.count >= 1;
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_extensions_parse_result
gremlinp_extensions_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_extensions_parse_result result = {0, 0, 0, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_EXTENSIONS, sizeof(KW_EXTENSIONS) - 1)) {
        result.error = GREMLINP_ERROR_UNEXPECTED_TOKEN;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    // First range (unrolled so the loop invariant can say count >= 1).
    struct gremlinp_range_parse_result first_range = gremlinp_lexems_parse_range(buf);
    if (first_range.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = first_range.error;
        return result;
    }
    size_t count = 1;

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    /*@ loop invariant buf->offset > start;
        loop invariant buf->offset <= buf->buf_size;
        loop invariant 1 <= count <= buf->offset - start;
        loop assigns buf->offset, count, errno, err;
        loop variant buf->buf_size - buf->offset;
    */
    while (gremlinp_parser_buffer_check_and_shift(buf, ',')) {
        err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

        struct gremlinp_range_parse_result range = gremlinp_lexems_parse_range(buf);
        if (range.error != GREMLINP_OK) {
            buf->offset = start;
            result.error = range.error;
            return result;
        }
        count++;

        err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }
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

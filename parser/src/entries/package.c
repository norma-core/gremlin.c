#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

static const char KW_PACKAGE[] = "package";

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset &&
               buf->buf[buf->offset - 1] == ';' &&
               \result.name_length > 0 &&
               valid_full_identifier(\result.name_start, \result.name_length);
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_package_parse_result
gremlinp_package_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_package_parse_result result = {NULL, 0, 0, 0, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_PACKAGE, sizeof(KW_PACKAGE) - 1)) {
        result.error = GREMLINP_ERROR_UNEXPECTED_TOKEN;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    struct gremlinp_span_parse_result name = gremlinp_lexems_parse_full_identifier(buf);
    if (name.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = name.error;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    if (!gremlinp_parser_buffer_check_and_shift(buf, ';')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_SEMICOLON_EXPECTED;
        return result;
    }

    result.name_start = name.start;
    result.name_length = name.length;
    result.start = start;
    result.end = buf->offset;
    return result;
}

#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

static const char KW_EDITION[] = "edition";

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset &&
               buf->buf[buf->offset - 1] == ';' &&
               \result.edition_length > 0;
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_edition_parse_result
gremlinp_edition_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_edition_parse_result result = {NULL, 0, 0, 0, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_EDITION, sizeof(KW_EDITION) - 1)) {
        result.error = GREMLINP_ERROR_INVALID_SYNTAX_DEF;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    if (!gremlinp_parser_buffer_check_and_shift(buf, '=')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_ASSIGNMENT_EXPECTED;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    struct gremlinp_span_parse_result edition_val = gremlinp_lexems_parse_string_literal(buf);
    if (edition_val.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = edition_val.error;
        return result;
    }

    if (edition_val.length == 0) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_INVALID_SYNTAX_DEF;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    if (!gremlinp_parser_buffer_check_and_shift(buf, ';')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_SEMICOLON_EXPECTED;
        return result;
    }

    result.edition_start = edition_val.start;
    result.edition_length = edition_val.length;
    result.start = start;
    result.end = buf->offset;
    return result;
}

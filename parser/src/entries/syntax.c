#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"
#include <string.h>

static const char KW_SYNTAX[] = "syntax";

/*@ requires len == 6 ==> \valid_read(start + (0 .. 5));
    assigns  \nothing;
    ensures  \result == \true <==>
               (len == 6 &&
                start[0] == 'p' && start[1] == 'r' && start[2] == 'o' &&
                start[3] == 't' && start[4] == 'o' &&
                (start[5] == '2' || start[5] == '3'));
*/
static bool
version_is_valid(const char *start, size_t len)
{
    return (len == 6 &&
            start[0] == 'p' && start[1] == 'r' && start[2] == 'o' &&
            start[3] == 't' && start[4] == 'o' &&
            (start[5] == '2' || start[5] == '3'));
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset &&
               buf->buf[buf->offset - 1] == ';' &&
               is_proto_version(\result.version_start, \result.version_length);
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_syntax_parse_result
gremlinp_syntax_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_syntax_parse_result result = {NULL, 0, 0, 0, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_SYNTAX, sizeof(KW_SYNTAX) - 1)) {
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

    struct gremlinp_span_parse_result version = gremlinp_lexems_parse_string_literal(buf);
    if (version.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = version.error;
        return result;
    }

    if (!version_is_valid(version.start, version.length)) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_INVALID_SYNTAX_VERSION;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    if (!gremlinp_parser_buffer_check_and_shift(buf, ';')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_SEMICOLON_EXPECTED;
        return result;
    }

    result.version_start = version.start;
    result.version_length = version.length;
    result.start = start;
    result.end = buf->offset;
    return result;
}

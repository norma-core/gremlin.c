#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

static const char KW_IMPORT[] = "import";
static const char KW_WEAK[]   = "weak";
static const char KW_PUBLIC[] = "public";

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset &&
               buf->buf[buf->offset - 1] == ';' &&
               \result.path_length > 0 &&
               (\result.type == GREMLINP_IMPORT_TYPE_REGULAR ||
                \result.type == GREMLINP_IMPORT_TYPE_WEAK ||
                \result.type == GREMLINP_IMPORT_TYPE_PUBLIC);
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_import_parse_result
gremlinp_import_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_import_parse_result result = {NULL, 0, GREMLINP_IMPORT_TYPE_REGULAR, 0, 0, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_IMPORT, sizeof(KW_IMPORT) - 1)) {
        result.error = GREMLINP_ERROR_UNEXPECTED_TOKEN;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    if (gremlinp_parser_buffer_check_str_and_shift(buf, KW_WEAK, sizeof(KW_WEAK) - 1)) {
        result.type = GREMLINP_IMPORT_TYPE_WEAK;
        err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }
    } else if (gremlinp_parser_buffer_check_str_and_shift(buf, KW_PUBLIC, sizeof(KW_PUBLIC) - 1)) {
        result.type = GREMLINP_IMPORT_TYPE_PUBLIC;
        err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }
    }

    struct gremlinp_span_parse_result path = gremlinp_lexems_parse_string_literal(buf);
    if (path.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = path.error;
        return result;
    }

    if (path.length == 0) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_IMPORT_PATH_INVALID;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    if (!gremlinp_parser_buffer_check_and_shift(buf, ';')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_SEMICOLON_EXPECTED;
        return result;
    }

    result.path_start = path.start;
    result.path_length = path.length;
    result.start = start;
    result.end = buf->offset;
    return result;
}

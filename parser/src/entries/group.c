#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

static const char KW_GROUP[]    = "group";
static const char KW_OPTIONAL[] = "optional";
static const char KW_REQUIRED[] = "required";
static const char KW_REPEATED[] = "repeated";
/*@ axiomatic Kw_group_nonempty { axiom kw_group_nonempty: KW_GROUP[0]    == 'g'; } */
/*@ axiomatic Kw_optional_nonempty { axiom kw_optional_nonempty: KW_OPTIONAL[0] == 'o'; } */
/*@ axiomatic Kw_required_nonempty { axiom kw_required_nonempty: KW_REQUIRED[0] == 'r'; } */
/*@ axiomatic Kw_repeated_nonempty { axiom kw_repeated_nonempty: KW_REPEATED[0] == 'r'; } */

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
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
struct gremlinp_group_parse_result
gremlinp_group_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_group_parse_result result = {0, 0, GREMLINP_OK};

    size_t start = buf->offset;

    // Optional label
    gremlinp_parser_buffer_check_str_and_shift(buf, KW_OPTIONAL);
    gremlinp_parser_buffer_check_str_and_shift(buf, KW_REQUIRED);
    gremlinp_parser_buffer_check_str_and_shift(buf, KW_REPEATED);

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_GROUP)) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_UNEXPECTED_TOKEN;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    // Parse group name
    struct gremlinp_span_parse_result name = gremlinp_lexems_parse_identifier(buf);
    if (name.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = name.error;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    if (!gremlinp_parser_buffer_check_and_shift(buf, '=')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_ASSIGNMENT_EXPECTED;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    // Parse field number
    struct gremlinp_int64_parse_result num = gremlinp_lexems_parse_integer_literal(buf);
    if (num.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = num.error;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    if (!gremlinp_parser_buffer_check_and_shift(buf, '{')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_BRACKET_EXPECTED;
        return result;
    }

    // Skip body
    int depth = 1;
    /*@ loop invariant depth >= 0;
        loop invariant buf->offset >= start;
        loop invariant buf->offset <= buf->buf_size;
        loop assigns buf->offset, depth;
        loop variant buf->buf_size - buf->offset;
    */
    while (depth > 0) {
        char c = gremlinp_parser_buffer_char(buf);
        if (c == '\0') {
            buf->offset = start;
            result.error = GREMLINP_ERROR_UNEXPECTED_EOF;
            return result;
        }
        buf->offset++;
        if (c == '{') depth++;
        else if (c == '}') depth--;
        else if (c == '"' || c == '\'') {
            /*@ loop invariant buf->offset >= start;
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
    }

    result.start = start;
    result.end = buf->offset;
    return result;
}

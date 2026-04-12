#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

struct gremlinp_option_item_result {
    const char                      *name_start;
    size_t                          name_length;
    struct gremlinp_const_parse_result value;
    enum gremlinp_parsing_error     error;
};

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.name_length > 0;
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
static struct gremlinp_option_item_result
parse_option_item(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_const_parse_result empty_val;
    empty_val.error = GREMLINP_OK;
    struct gremlinp_option_item_result result = {NULL, 0, empty_val, GREMLINP_OK};

    size_t item_start = buf->offset;

    struct gremlinp_span_parse_result name = gremlinp_lexems_parse_full_identifier(buf);
    if (name.error != GREMLINP_OK) {
        result.error = GREMLINP_ERROR_INVALID_OPTION_NAME;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = item_start; result.error = err; return result; }

    if (!gremlinp_parser_buffer_check_and_shift(buf, '=')) {
        buf->offset = item_start;
        result.error = GREMLINP_ERROR_ASSIGNMENT_EXPECTED;
        return result;
    }

    err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = item_start; result.error = err; return result; }

    struct gremlinp_const_parse_result val = gremlinp_lexems_parse_const_value(buf);
    if (val.error != GREMLINP_OK) {
        buf->offset = item_start;
        result.error = val.error;
        return result;
    }

    result.name_start = name.start;
    result.name_length = name.length;
    result.value = val;
    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset >= \old(buf->offset) + 2 &&
               buf->buf[\old(buf->offset)] == '[' &&
               buf->buf[buf->offset - 1] == ']' &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset;
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_option_list_result
gremlinp_option_list_consume(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_option_list_result result = {0, 0, 0, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_and_shift(buf, '[')) {
        result.error = GREMLINP_ERROR_UNEXPECTED_TOKEN;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

    if (gremlinp_parser_buffer_check_and_shift(buf, ']')) {
        result.start = start;
        result.end = buf->offset;
        result.count = 0;
        return result;
    }

    size_t count = 0;

    /*@ loop invariant buf->offset > start;
        loop invariant buf->offset <= buf->buf_size;
        loop invariant count >= 0;
        loop assigns buf->offset, count, errno, err;
        loop variant buf->buf_size - buf->offset;
    */
    while (true) {
        /*@ assert \separated(&count, &start, buf, &errno); */
        struct gremlinp_option_item_result item = parse_option_item(buf);
        if (item.error != GREMLINP_OK) {
            buf->offset = start;
            result.error = item.error;
            return result;
        }
        count++;

        err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

        if (gremlinp_parser_buffer_check_and_shift(buf, ']')) {
            break;
        }

        if (!gremlinp_parser_buffer_check_and_shift(buf, ',')) {
            buf->offset = start;
            result.error = GREMLINP_ERROR_INVALID_CHARACTER;
            return result;
        }

        err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }
    }

    result.start = start;
    result.end = buf->offset;
    result.count = count;
    return result;
}

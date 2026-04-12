#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset &&
               (\result.kind == GREMLINP_FIELD_TYPE_NAMED ||
                \result.kind == GREMLINP_FIELD_TYPE_MAP);
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_field_type_parse_result
gremlinp_field_type_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_span_parse_result empty_span = {NULL, 0, GREMLINP_OK};
    struct gremlinp_field_type_parse_result result;
    result.u.named = empty_span;
    result.kind = GREMLINP_FIELD_TYPE_NAMED;
    result.start = 0;
    result.end = 0;
    result.error = GREMLINP_OK;

    size_t start = buf->offset;

    // Try map<K,V> first
    struct gremlinp_map_type_parse_result map = gremlinp_lexems_parse_map_type(buf);
    if (map.error == GREMLINP_OK) {
        result.kind = GREMLINP_FIELD_TYPE_MAP;
        result.u.map = map;
        result.start = start;
        result.end = buf->offset;
        return result;
    }

    // Fall back to named type (identifier)
    struct gremlinp_span_parse_result named = gremlinp_lexems_parse_full_identifier(buf);
    if (named.error == GREMLINP_OK) {
        result.kind = GREMLINP_FIELD_TYPE_NAMED;
        result.u.named = named;
        result.start = start;
        result.end = buf->offset;
        return result;
    }

    result.error = GREMLINP_ERROR_INVALID_FIELD_NAME;
    return result;
}

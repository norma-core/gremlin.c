#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

static const char KW_MESSAGE[] = "message";
/*@ axiomatic Kw_message_nonempty { axiom kw_message_nonempty: KW_MESSAGE[0] == 'm'; } */

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.name_length > 0 &&
               buf->buf[buf->offset - 1] == '}' &&
               \result.start == \old(buf->offset) &&
               \result.end == buf->offset;
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_message_parse_result
gremlinp_message_parse(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_message_parse_result result = {NULL, 0, 0, 0, 0, 0, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_MESSAGE)) {
        result.error = GREMLINP_ERROR_UNEXPECTED_TOKEN;
        return result;
    }

    enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
    if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

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

    size_t body_start = buf->offset;

    // Flat loop: depth tracks nesting of message/enum/oneof/extend/group blocks
    int depth = 1;

    /*@ loop invariant depth >= 1;
        loop invariant buf->offset >= body_start;
        loop invariant buf->offset <= buf->buf_size;
        loop assigns buf->offset, errno, err, depth;
        loop variant buf->buf_size - buf->offset;
    */
    while (depth >= 1) {
        err = gremlinp_parser_buffer_skip_spaces(buf);
        if (err != GREMLINP_OK) { buf->offset = start; result.error = err; return result; }

        char c = gremlinp_parser_buffer_char(buf);

        if (c == '\0') {
            buf->offset = start;
            result.error = GREMLINP_ERROR_UNEXPECTED_EOF;
            return result;
        }

        if (c == '}') {
            buf->offset++;
            depth--;
            continue;
        }

        if (c == ';') {
            buf->offset++;
            continue;
        }

        // Try entries that open new blocks (increase depth)
        // These parse header + '{' only, body handled by this flat loop

        // Try enum (has its own body parsing, returns after '}')
        struct gremlinp_enum_parse_result en = gremlinp_enum_parse(buf);
        if (en.error == GREMLINP_OK) continue;

        // Try nested message header: message Name {
        size_t msg_save = buf->offset;
        if (gremlinp_parser_buffer_check_str_and_shift(buf, KW_MESSAGE)) {
            err = gremlinp_parser_buffer_skip_spaces(buf);
            if (err == GREMLINP_OK) {
                struct gremlinp_span_parse_result mname = gremlinp_lexems_parse_identifier(buf);
                if (mname.error == GREMLINP_OK) {
                    err = gremlinp_parser_buffer_skip_spaces(buf);
                    if (err == GREMLINP_OK && gremlinp_parser_buffer_check_and_shift(buf, '{')) {
                        depth++;
                        continue;
                    }
                }
            }
            buf->offset = msg_save;
        }

        // Try oneof (has its own body parsing)
        struct gremlinp_oneof_parse_result oneof = gremlinp_oneof_parse(buf);
        if (oneof.error == GREMLINP_OK) continue;

        // Try option
        struct gremlinp_option_parse_result opt = gremlinp_option_parse(buf);
        if (opt.error == GREMLINP_OK) continue;

        // Try extensions
        struct gremlinp_extensions_parse_result ext = gremlinp_extensions_parse(buf);
        if (ext.error == GREMLINP_OK) continue;

        // Try reserved
        struct gremlinp_reserved_parse_result res = gremlinp_reserved_parse(buf);
        if (res.error == GREMLINP_OK) continue;

        // Try extend (has its own body parsing)
        struct gremlinp_extend_parse_result extend = gremlinp_extend_parse(buf);
        if (extend.error == GREMLINP_OK) continue;

        // Try group (has its own body parsing)
        struct gremlinp_group_parse_result grp = gremlinp_group_parse(buf);
        if (grp.error == GREMLINP_OK) continue;

        // Try field (most general, last)
        struct gremlinp_field_parse_result field = gremlinp_field_parse(buf);
        if (field.error == GREMLINP_OK) continue;

        buf->offset = start;
        result.error = GREMLINP_ERROR_INVALID_CHARACTER;
        return result;
    }

    result.name_start = name.start;
    result.name_length = name.length;
    result.body_start = body_start;
    result.body_end = buf->offset - 1;
    result.start = start;
    result.end = buf->offset;
    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start >= \old(buf->offset) &&
               \result.end == buf->offset &&
               (\result.kind == GREMLINP_MSG_ENTRY_FIELD ||
                \result.kind == GREMLINP_MSG_ENTRY_ENUM ||
                \result.kind == GREMLINP_MSG_ENTRY_MESSAGE ||
                \result.kind == GREMLINP_MSG_ENTRY_ONEOF ||
                \result.kind == GREMLINP_MSG_ENTRY_OPTION ||
                \result.kind == GREMLINP_MSG_ENTRY_EXTENSIONS ||
                \result.kind == GREMLINP_MSG_ENTRY_RESERVED ||
                \result.kind == GREMLINP_MSG_ENTRY_EXTEND ||
                \result.kind == GREMLINP_MSG_ENTRY_GROUP);
    ensures  \result.error == GREMLINP_OK ==>
               (\result.kind == GREMLINP_MSG_ENTRY_FIELD      ==> \result.u.field.error      == GREMLINP_OK) &&
               (\result.kind == GREMLINP_MSG_ENTRY_ENUM       ==> \result.u.enumeration.error == GREMLINP_OK) &&
               (\result.kind == GREMLINP_MSG_ENTRY_MESSAGE    ==> \result.u.message.error    == GREMLINP_OK) &&
               (\result.kind == GREMLINP_MSG_ENTRY_ONEOF      ==> \result.u.oneof.error      == GREMLINP_OK) &&
               (\result.kind == GREMLINP_MSG_ENTRY_OPTION     ==> \result.u.option.error     == GREMLINP_OK) &&
               (\result.kind == GREMLINP_MSG_ENTRY_EXTENSIONS ==> \result.u.extensions.error == GREMLINP_OK) &&
               (\result.kind == GREMLINP_MSG_ENTRY_RESERVED   ==> \result.u.reserved.error   == GREMLINP_OK) &&
               (\result.kind == GREMLINP_MSG_ENTRY_EXTEND     ==> \result.u.extend.error     == GREMLINP_OK) &&
               (\result.kind == GREMLINP_MSG_ENTRY_GROUP      ==> \result.u.group.error      == GREMLINP_OK);
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_message_entry_result
gremlinp_message_next_entry(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_message_entry_result result;
    result.kind = GREMLINP_MSG_ENTRY_FIELD;
    result.start = 0;
    result.end = 0;
    result.error = GREMLINP_OK;

    size_t saved = buf->offset;

    gremlinp_parser_buffer_skip_spaces(buf);

    /*@ loop invariant buf->offset >= saved;
        loop invariant buf->offset <= buf->buf_size;
        loop assigns buf->offset;
        loop variant buf->buf_size - buf->offset;
    */
    while (gremlinp_parser_buffer_char(buf) == ';') {
        buf->offset++;
        gremlinp_parser_buffer_skip_spaces(buf);
    }

    size_t entry_start = buf->offset;
    /*@ assert \separated(&saved, &entry_start, buf, &errno); */

    struct gremlinp_enum_parse_result en = gremlinp_enum_parse(buf);
    if (en.error == GREMLINP_OK) {
        result.kind = GREMLINP_MSG_ENTRY_ENUM;
        result.u.enumeration = en;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_message_parse_result msg = gremlinp_message_parse(buf);
    if (msg.error == GREMLINP_OK) {
        result.kind = GREMLINP_MSG_ENTRY_MESSAGE;
        result.u.message = msg;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_oneof_parse_result oneof = gremlinp_oneof_parse(buf);
    if (oneof.error == GREMLINP_OK) {
        result.kind = GREMLINP_MSG_ENTRY_ONEOF;
        result.u.oneof = oneof;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_option_parse_result opt = gremlinp_option_parse(buf);
    if (opt.error == GREMLINP_OK) {
        result.kind = GREMLINP_MSG_ENTRY_OPTION;
        result.u.option = opt;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_extensions_parse_result ext = gremlinp_extensions_parse(buf);
    if (ext.error == GREMLINP_OK) {
        result.kind = GREMLINP_MSG_ENTRY_EXTENSIONS;
        result.u.extensions = ext;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_reserved_parse_result res = gremlinp_reserved_parse(buf);
    if (res.error == GREMLINP_OK) {
        result.kind = GREMLINP_MSG_ENTRY_RESERVED;
        result.u.reserved = res;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_extend_parse_result extend = gremlinp_extend_parse(buf);
    if (extend.error == GREMLINP_OK) {
        result.kind = GREMLINP_MSG_ENTRY_EXTEND;
        result.u.extend = extend;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_group_parse_result grp = gremlinp_group_parse(buf);
    if (grp.error == GREMLINP_OK) {
        result.kind = GREMLINP_MSG_ENTRY_GROUP;
        result.u.group = grp;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_field_parse_result field = gremlinp_field_parse(buf);
    if (field.error == GREMLINP_OK) {
        result.kind = GREMLINP_MSG_ENTRY_FIELD;
        result.u.field = field;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    buf->offset = saved;
    result.error = GREMLINP_ERROR_INVALID_CHARACTER;
    return result;
}

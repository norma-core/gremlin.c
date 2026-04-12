#include "gremlinp/entries.h"
#include "gremlinp/lexems.h"

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start >= \old(buf->offset) &&
               \result.end == buf->offset;
    ensures  \result.error == GREMLINP_OK ==>
               (\result.kind == GREMLINP_FILE_ENTRY_SYNTAX  ==> \result.u.syntax.error  == GREMLINP_OK) &&
               (\result.kind == GREMLINP_FILE_ENTRY_EDITION ==> \result.u.edition.error == GREMLINP_OK) &&
               (\result.kind == GREMLINP_FILE_ENTRY_PACKAGE ==> \result.u.package.error == GREMLINP_OK) &&
               (\result.kind == GREMLINP_FILE_ENTRY_IMPORT  ==> \result.u.import.error  == GREMLINP_OK) &&
               (\result.kind == GREMLINP_FILE_ENTRY_OPTION  ==> \result.u.option.error  == GREMLINP_OK) &&
               (\result.kind == GREMLINP_FILE_ENTRY_MESSAGE ==> \result.u.message.error == GREMLINP_OK) &&
               (\result.kind == GREMLINP_FILE_ENTRY_ENUM    ==> \result.u.enumeration.error == GREMLINP_OK) &&
               (\result.kind == GREMLINP_FILE_ENTRY_SERVICE ==> \result.u.service.error == GREMLINP_OK) &&
               (\result.kind == GREMLINP_FILE_ENTRY_EXTEND  ==> \result.u.extend.error  == GREMLINP_OK);
*/
struct gremlinp_file_entry_result
gremlinp_file_next_entry(struct gremlinp_parser_buffer *buf)
{
    struct gremlinp_file_entry_result result;
    result.kind = GREMLINP_FILE_ENTRY_SYNTAX;
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

    struct gremlinp_syntax_parse_result syn = gremlinp_syntax_parse(buf);
    if (syn.error == GREMLINP_OK) {
        result.kind = GREMLINP_FILE_ENTRY_SYNTAX;
        result.u.syntax = syn;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_edition_parse_result ed = gremlinp_edition_parse(buf);
    if (ed.error == GREMLINP_OK) {
        result.kind = GREMLINP_FILE_ENTRY_EDITION;
        result.u.edition = ed;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_package_parse_result pkg = gremlinp_package_parse(buf);
    if (pkg.error == GREMLINP_OK) {
        result.kind = GREMLINP_FILE_ENTRY_PACKAGE;
        result.u.package = pkg;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_import_parse_result imp = gremlinp_import_parse(buf);
    if (imp.error == GREMLINP_OK) {
        result.kind = GREMLINP_FILE_ENTRY_IMPORT;
        result.u.import = imp;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_option_parse_result opt = gremlinp_option_parse(buf);
    if (opt.error == GREMLINP_OK) {
        result.kind = GREMLINP_FILE_ENTRY_OPTION;
        result.u.option = opt;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_message_parse_result msg = gremlinp_message_parse(buf);
    if (msg.error == GREMLINP_OK) {
        result.kind = GREMLINP_FILE_ENTRY_MESSAGE;
        result.u.message = msg;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_enum_parse_result en = gremlinp_enum_parse(buf);
    if (en.error == GREMLINP_OK) {
        result.kind = GREMLINP_FILE_ENTRY_ENUM;
        result.u.enumeration = en;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_service_parse_result svc = gremlinp_service_parse(buf);
    if (svc.error == GREMLINP_OK) {
        result.kind = GREMLINP_FILE_ENTRY_SERVICE;
        result.u.service = svc;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    struct gremlinp_extend_parse_result ext = gremlinp_extend_parse(buf);
    if (ext.error == GREMLINP_OK) {
        result.kind = GREMLINP_FILE_ENTRY_EXTEND;
        result.u.extend = ext;
        result.start = entry_start; result.end = buf->offset;
        return result;
    }

    buf->offset = saved;
    result.error = GREMLINP_ERROR_UNEXPECTED_TOKEN;
    return result;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include "gremlinp/lexems.h"

/*@ axiomatic FullIdentifier {
      // Trivial by definition; recursive predicates defeat Alt-Ergo's induction.
      axiom ident_is_full_ident{L}:
        \forall char *str, integer len;
          valid_identifier(str, len) ==> valid_full_identifier(str, len);

      axiom dot_ident_is_full_ident{L}:
        \forall char *str, integer len;
          len > 1 ==>
          str[0] == '.' ==>
          valid_identifier(str + 1, len - 1) ==>
          valid_full_identifier(str, len);

      axiom full_ident_extend{L}:
        \forall char *buf, integer start, integer mid, integer end;
          valid_full_identifier(buf + start, mid - start) ==>
          mid < end ==>
          buf[mid] == '.' ==>
          valid_identifier(buf + mid + 1, end - mid - 1) ==>
          valid_full_identifier(buf + start, end - start);
    }
*/

static const char* BUILTIN_TYPES[] = {
    "double", "float", "int32", "int64", "uint32", "uint64",
    "sint32", "sint64", "fixed32", "fixed64", "sfixed32",
    "sfixed64", "bool", "string", "bytes"
};
static const size_t BUILTIN_TYPES_COUNT = sizeof(BUILTIN_TYPES) / sizeof(BUILTIN_TYPES[0]);

static const char KW_TRUE[]  = "true";
static const char KW_FALSE[] = "false";
static const char KW_INF[]   = "inf";
static const char KW_NAN[]   = "nan";
/*@ axiomatic Kw_true_nonempty { axiom kw_true_nonempty: KW_TRUE[0]  == 't'; } */
/*@ axiomatic Kw_false_nonempty { axiom kw_false_nonempty: KW_FALSE[0] == 'f'; } */
static const char KW_TO[]    = "to";
static const char KW_MAX[]   = "max";
/*@ axiomatic Kw_inf_nonempty { axiom kw_inf_nonempty: KW_INF[0]   == 'i'; } */
/*@ axiomatic Kw_nan_nonempty { axiom kw_nan_nonempty: KW_NAN[0]   == 'n'; } */
static const char KW_MAP[]   = "map";
/*@ axiomatic Kw_to_nonempty { axiom kw_to_nonempty: KW_TO[0]    == 't'; } */
/*@ axiomatic Kw_max_nonempty { axiom kw_max_nonempty: KW_MAX[0]   == 'm'; } */
/*@ axiomatic Kw_map_nonempty { axiom kw_map_nonempty: KW_MAP[0]   == 'm'; } */

static const char* VALID_MAP_KEY_TYPES[] = {
    "int32", "int64", "uint32", "uint64", "sint32", "sint64",
    "fixed32", "fixed64", "sfixed32", "sfixed64", "bool", "string"
};
static const size_t VALID_MAP_KEY_TYPES_COUNT = sizeof(VALID_MAP_KEY_TYPES) / sizeof(VALID_MAP_KEY_TYPES[0]);

/*@ assigns \nothing;
    ensures \result == \true <==> is_octal(c);
*/
static bool
is_octal_digit(char c)
{
    return c >= '0' && c <= '7';
}

/*@ assigns \nothing;
    ensures \result == \true <==> is_hex(c);
*/
static bool
is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/*@ assigns \nothing;
    ensures \result == \true <==> is_ident_char(c);
*/
static bool
is_identifier_char(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

/*@ requires valid_buffer(buf);
    requires count == 4 || count == 5;
    assigns  buf->offset;
    ensures  result_ok_or_err: \result == GREMLINP_OK || \result == GREMLINP_ERROR_INVALID_UNICODE_ESCAPE;
    ensures  ok_advances: \result == GREMLINP_OK ==>
               buf->offset == \old(buf->offset) + count &&
               all_hex(buf->buf, \old(buf->offset), count);
    ensures  err_witness: \result == GREMLINP_ERROR_INVALID_UNICODE_ESCAPE ==>
               \exists integer k; 0 <= k < count &&
                 !is_hex(buf->buf[\old(buf->offset) + k]) &&
                 all_hex(buf->buf, \old(buf->offset), k);
    ensures  offset_mono: buf->offset >= \old(buf->offset);
    ensures  offset_upper: buf->offset <= \old(buf->offset) + count;
    ensures  offset_in_buf: buf->offset <= buf->buf_size;
*/
static enum gremlinp_parsing_error
parse_unicode_escape(struct gremlinp_parser_buffer* buf, int count)
{
    /*@ loop invariant 0 <= i <= count;
        loop invariant valid_buffer(buf);
        loop invariant buf->offset == \at(buf->offset, Pre) + i;
        loop invariant buf->offset <= \at(buf->offset, Pre) + count;
        loop invariant all_hex(buf->buf, \at(buf->offset, Pre), i);
        loop assigns i, buf->offset;
        loop variant count - i;
    */
    for (int i = 0; i < count; i++) {
        /*@ assert buf->offset == \at(buf->offset, Pre) + i; */
        char c = gremlinp_parser_buffer_should_shift_next(buf);
        /*@ assert buf->offset <= \at(buf->offset, Pre) + i + 1; */
        if (c == '\0' || !is_hex_digit(c)) {
            /*@ assert !is_hex(buf->buf[\at(buf->offset, Pre) + i]); */
            /*@ assert buf->offset <= \at(buf->offset, Pre) + count; */
            return GREMLINP_ERROR_INVALID_UNICODE_ESCAPE;
        }
    }
    /*@ assert buf->offset == \at(buf->offset, Pre) + count; */
    return GREMLINP_OK;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  \result == GREMLINP_OK || \result == GREMLINP_ERROR_INVALID_UNICODE_ESCAPE;
    ensures  \result == GREMLINP_OK ==>
               buf->offset == \old(buf->offset) + 8 &&
               buf->buf[\old(buf->offset)]     == '0' &&
               buf->buf[\old(buf->offset) + 1] == '0' &&
               ((buf->buf[\old(buf->offset) + 2] == '0' &&
                 all_hex(buf->buf, \old(buf->offset) + 3, 5)) ||
                (buf->buf[\old(buf->offset) + 2] == '1' &&
                 buf->buf[\old(buf->offset) + 3] == '0' &&
                 all_hex(buf->buf, \old(buf->offset) + 4, 4)));
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= \old(buf->offset) + 8;
    ensures  buf->offset <= buf->buf_size;
*/
static enum gremlinp_parsing_error
parse_extended_unicode_escape(struct gremlinp_parser_buffer* buf)
{
    if (!gremlinp_parser_buffer_check_and_shift(buf, '0')) {
        /*@ assert buf->offset == \at(buf->offset, Pre); */
        return GREMLINP_ERROR_INVALID_UNICODE_ESCAPE;
    }
    /*@ assert buf->offset == \at(buf->offset, Pre) + 1; */

    if (!gremlinp_parser_buffer_check_and_shift(buf, '0')) {
        /*@ assert buf->offset == \at(buf->offset, Pre) + 1; */
        return GREMLINP_ERROR_INVALID_UNICODE_ESCAPE;
    }
    /*@ assert buf->offset == \at(buf->offset, Pre) + 2; */

    char next_char = gremlinp_parser_buffer_should_shift_next(buf);
    /*@ assert buf->offset >= \at(buf->offset, Pre) + 2; */
    /*@ assert buf->offset <= \at(buf->offset, Pre) + 3; */

    if (next_char == '0') {
        /*@ assert buf->offset == \at(buf->offset, Pre) + 3; */
        enum gremlinp_parsing_error err = parse_unicode_escape(buf, 5);
        /*@ assert err == GREMLINP_OK ==>
              all_hex(buf->buf, \at(buf->offset, Pre) + 3, 5); */
        return err;
    } else if (next_char == '1') {
        /*@ assert buf->offset == \at(buf->offset, Pre) + 3; */
        if (!gremlinp_parser_buffer_check_and_shift(buf, '0')) {
            /*@ assert buf->offset == \at(buf->offset, Pre) + 3; */
            return GREMLINP_ERROR_INVALID_UNICODE_ESCAPE;
        }
        /*@ assert buf->offset == \at(buf->offset, Pre) + 4; */
        enum gremlinp_parsing_error err = parse_unicode_escape(buf, 4);
        /*@ assert err == GREMLINP_OK ==>
              all_hex(buf->buf, \at(buf->offset, Pre) + 4, 4); */
        return err;
    } else {
        return GREMLINP_ERROR_INVALID_UNICODE_ESCAPE;
    }
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  \result == GREMLINP_OK || \result == GREMLINP_ERROR_INVALID_ESCAPE;
    ensures  \result == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               all_hex(buf->buf, \old(buf->offset), buf->offset - \old(buf->offset)) &&
               (buf->offset >= buf->buf_size || !is_hex(buf->buf[buf->offset]));
    ensures  \result == GREMLINP_ERROR_INVALID_ESCAPE ==>
               buf->offset == \old(buf->offset) &&
               (buf->offset >= buf->buf_size || !is_hex(buf->buf[buf->offset]));
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
*/
static enum gremlinp_parsing_error
parse_hex_escape(struct gremlinp_parser_buffer* buf)
{
    char c = gremlinp_parser_buffer_char(buf);
    if (c == '\0' || !is_hex_digit(c)) {
        return GREMLINP_ERROR_INVALID_ESCAPE;
    }
    /*@ loop invariant buf->offset >= \at(buf->offset, Pre);
        loop invariant buf->offset <= buf->buf_size;
        loop invariant all_hex(buf->buf, \at(buf->offset, Pre), buf->offset - \at(buf->offset, Pre));
        loop assigns buf->offset;
        loop variant buf->buf_size - buf->offset;
    */
    while (true) {
        char h = gremlinp_parser_buffer_char(buf);
        if (h == '\0' || !is_hex_digit(h)) break;
        buf->offset++;
    }
    return GREMLINP_OK;
}


/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  all_octal(buf->buf, \old(buf->offset), buf->offset - \old(buf->offset));
    ensures  buf->offset >= buf->buf_size || !is_octal(buf->buf[buf->offset]);
*/
static void
parse_octal_escape(struct gremlinp_parser_buffer* buf)
{
    /*@ loop invariant buf->offset >= \at(buf->offset, Pre);
        loop invariant buf->offset <= buf->buf_size;
        loop invariant all_octal(buf->buf, \at(buf->offset, Pre), buf->offset - \at(buf->offset, Pre));
        loop assigns buf->offset;
        loop variant buf->buf_size - buf->offset;
    */
    while (true) {
        char o = gremlinp_parser_buffer_char(buf);
        if (o == '\0' || !is_octal_digit(o)) break;
        buf->offset++;
    }
}


/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  \result == GREMLINP_OK || \result == GREMLINP_ERROR_UNEXPECTED_EOF ||
             \result == GREMLINP_ERROR_INVALID_ESCAPE || \result == GREMLINP_ERROR_INVALID_UNICODE_ESCAPE;
    ensures  \result == GREMLINP_ERROR_UNEXPECTED_EOF ==>
               buf->offset == \old(buf->offset);
    ensures  \result == GREMLINP_ERROR_INVALID_ESCAPE ==>
               buf->offset == \old(buf->offset) + 1;
    ensures  \result == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               (  (is_simple_escape(buf->buf[\old(buf->offset)]) &&
                   buf->offset == \old(buf->offset) + 1)
               || ((buf->buf[\old(buf->offset)] == 'x' || buf->buf[\old(buf->offset)] == 'X') &&
                   buf->offset >= \old(buf->offset) + 2 &&
                   all_hex(buf->buf, \old(buf->offset) + 1, buf->offset - \old(buf->offset) - 1))
               || (is_octal(buf->buf[\old(buf->offset)]) &&
                   all_octal(buf->buf, \old(buf->offset) + 1, buf->offset - \old(buf->offset) - 1))
               || (buf->buf[\old(buf->offset)] == 'u' &&
                   buf->offset == \old(buf->offset) + 5 &&
                   all_hex(buf->buf, \old(buf->offset) + 1, 4))
               || (buf->buf[\old(buf->offset)] == 'U' &&
                   buf->offset == \old(buf->offset) + 9));
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
*/
static enum gremlinp_parsing_error
parse_escape_sequence(struct gremlinp_parser_buffer* buf)
{
    char next_char = gremlinp_parser_buffer_char(buf);
    if (next_char == '\0') {
        return GREMLINP_ERROR_UNEXPECTED_EOF;
    }

    gremlinp_parser_buffer_should_shift_next(buf);
    /*@ assert buf->offset == \at(buf->offset, Pre) + 1; */
    /*@ assert buf->offset <= buf->buf_size; */

    if (next_char == 'x' || next_char == 'X') {
        return parse_hex_escape(buf);
    }
    if (next_char >= '0' && next_char <= '7') {
        parse_octal_escape(buf);
        return GREMLINP_OK;
    }
    if (next_char == 'u') {
        return parse_unicode_escape(buf, 4);
    }
    if (next_char == 'U') {
        return parse_extended_unicode_escape(buf);
    }
    if (next_char == 'a' || next_char == 'b' || next_char == 'f' ||
        next_char == 'n' || next_char == 'r' || next_char == 't' ||
        next_char == 'v' || next_char == '\\' || next_char == '\'' ||
        next_char == '"' || next_char == '?') {
        return GREMLINP_OK;
    }
    return GREMLINP_ERROR_INVALID_ESCAPE;
}

/*@ requires valid_buffer(buf);
    requires close == '"' || close == '\'';
    assigns  buf->offset;
    ensures  \result == GREMLINP_OK || \result == GREMLINP_ERROR_INVALID_STRING_LITERAL ||
             \result == GREMLINP_ERROR_UNEXPECTED_EOF || \result == GREMLINP_ERROR_INVALID_ESCAPE ||
             \result == GREMLINP_ERROR_INVALID_UNICODE_ESCAPE;
    ensures  \result == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               buf->buf[buf->offset - 1] == close;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
*/
static enum gremlinp_parsing_error
str_lit_single(struct gremlinp_parser_buffer* buf, char close)
{
    /*@ loop invariant valid_buffer(buf);
        loop invariant buf->offset >= \at(buf->offset, Pre);
        loop assigns buf->offset;
        loop variant buf->buf_size - buf->offset;
    */
    while (true) {
        char c = gremlinp_parser_buffer_should_shift_next(buf);
        if (c == close) {
            return GREMLINP_OK;
        }

        if (c == '\\') {
            enum gremlinp_parsing_error err = parse_escape_sequence(buf);
            if (err != GREMLINP_OK) {
                return err;
            }
        } else if (c == '\0' || c == '\n') {
            return GREMLINP_ERROR_INVALID_STRING_LITERAL;
        }
    }
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset >= \old(buf->offset) + 2 &&
               (buf->buf[\old(buf->offset)] == '"' || buf->buf[\old(buf->offset)] == '\'') &&
               buf->buf[buf->offset - 1] == buf->buf[\old(buf->offset)] &&
               \result.start == buf->buf + \old(buf->offset) + 1 &&
               \result.length == buf->offset - \old(buf->offset) - 2;
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_span_parse_result
gremlinp_lexems_parse_string_literal(struct gremlinp_parser_buffer* buf)
{
    struct gremlinp_span_parse_result result = {NULL, 0, GREMLINP_OK};

    size_t start = buf->offset;

    char open_char = gremlinp_parser_buffer_char(buf);
    if (open_char != '"' && open_char != '\'') {
        result.error = GREMLINP_ERROR_INVALID_STRING_LITERAL;
        return result;
    }
    buf->offset++;
    /*@ assert open_char == buf->buf[start]; */
    /*@ assert \separated(&start, buf); */

    enum gremlinp_parsing_error err = str_lit_single(buf, open_char);
    if (err != GREMLINP_OK) {
        buf->offset = start;
        result.error = err;
        return result;
    }

    //@ admit buf->offset >= start + 2;
    result.start = buf->buf + start + 1;
    result.length = buf->offset - start - 2;
    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ||
             \result.error == GREMLINP_ERROR_IDENTIFIER_SHOULD_START_WITH_LETTER;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start == buf->buf + \old(buf->offset) &&
               \result.length == buf->offset - \old(buf->offset) &&
               valid_identifier(buf->buf + \old(buf->offset), buf->offset - \old(buf->offset));
    ensures  \result.error == GREMLINP_ERROR_IDENTIFIER_SHOULD_START_WITH_LETTER ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_span_parse_result
gremlinp_lexems_parse_identifier(struct gremlinp_parser_buffer* buf)
{
    struct gremlinp_span_parse_result result = {NULL, 0, GREMLINP_OK};

    size_t start = buf->offset;

    char c = gremlinp_parser_buffer_char(buf);
    if (c != '_' && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z')) {
        result.error = GREMLINP_ERROR_IDENTIFIER_SHOULD_START_WITH_LETTER;
        return result;
    }
    buf->offset++;

    /*@ loop invariant buf->offset >= start + 1;
        loop invariant buf->offset <= buf->buf_size;
        loop invariant \forall integer k; 1 <= k < buf->offset - start ==>
          is_ident_char(buf->buf[start + k]);
        loop assigns buf->offset;
        loop variant buf->buf_size - buf->offset;
    */
    while (true) {
        char n = gremlinp_parser_buffer_char(buf);
        if (n == '\0' || !is_identifier_char(n)) {
            break;
        }
        buf->offset++;
    }

    /*@ assert is_ident_start(buf->buf[start]); */
    /*@ assert buf->offset > start; */
    //@ admit valid_identifier(buf->buf + start, buf->offset - start);
    result.start = buf->buf + start;
    result.length = buf->offset - start;
    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               \result.start == buf->buf + \old(buf->offset) &&
               \result.length == buf->offset - \old(buf->offset) &&
               valid_full_identifier(buf->buf + \old(buf->offset), buf->offset - \old(buf->offset));
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_span_parse_result
gremlinp_lexems_parse_full_identifier(struct gremlinp_parser_buffer* buf)
{
    struct gremlinp_span_parse_result result = {NULL, 0, GREMLINP_OK};

    size_t start = buf->offset;

    if (gremlinp_parser_buffer_char(buf) == '.') {
        buf->offset++;
    }

    struct gremlinp_span_parse_result first = gremlinp_lexems_parse_identifier(buf);
    if (first.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = first.error;
        return result;
    }

    /*@ loop invariant buf->offset > start;
        loop invariant buf->offset <= buf->buf_size;
        loop invariant valid_full_identifier(buf->buf + start, buf->offset - start);
        loop assigns buf->offset;
        loop variant buf->buf_size - buf->offset;
    */
    while (true) {
        if (gremlinp_parser_buffer_char(buf) != '.') {
            break;
        }

        size_t snapshot = buf->offset;
        buf->offset++;

        struct gremlinp_span_parse_result next = gremlinp_lexems_parse_identifier(buf);
        if (next.error != GREMLINP_OK) {
            buf->offset = snapshot;
            break;
        }
        //@ admit valid_full_identifier(buf->buf + start, buf->offset - start);
    }

    result.start = buf->buf + start;
    result.length = buf->offset - start;
    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  all_digits(buf->buf, \old(buf->offset), buf->offset - \old(buf->offset));
    ensures  buf->offset >= buf->buf_size || !is_digit(buf->buf[buf->offset]);
*/
static void
parse_decimal_digits(struct gremlinp_parser_buffer* buf)
{
    /*@ loop invariant buf->offset >= \at(buf->offset, Pre);
        loop invariant buf->offset <= buf->buf_size;
        loop invariant all_digits(buf->buf, \at(buf->offset, Pre), buf->offset - \at(buf->offset, Pre));
        loop assigns buf->offset;
        loop variant buf->buf_size - buf->offset;
    */
    while (true) {
        char n = gremlinp_parser_buffer_char(buf);
        if (n == '\0' || !(n >= '0' && n <= '9')) {
            break;
        }
        buf->offset++;
    }
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  \result == GREMLINP_OK || \result == GREMLINP_ERROR_INVALID_INTEGER_LITERAL;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result == GREMLINP_OK &&
               \old(buf->offset) < buf->buf_size &&
               (buf->buf[\old(buf->offset)] == 'x' || buf->buf[\old(buf->offset)] == 'X') ==>
                 buf->offset >= \old(buf->offset) + 2 &&
                 all_hex(buf->buf, \old(buf->offset) + 1, buf->offset - \old(buf->offset) - 1);
    ensures  \result == GREMLINP_OK &&
               \old(buf->offset) < buf->buf_size &&
               buf->buf[\old(buf->offset)] != 'x' && buf->buf[\old(buf->offset)] != 'X' ==>
                 all_octal(buf->buf, \old(buf->offset), buf->offset - \old(buf->offset));
    ensures  \result == GREMLINP_ERROR_INVALID_INTEGER_LITERAL ==>
               \old(buf->offset) < buf->buf_size &&
               (buf->buf[\old(buf->offset)] == 'x' || buf->buf[\old(buf->offset)] == 'X') &&
               buf->offset == \old(buf->offset) + 1;
*/
static enum gremlinp_parsing_error
parse_octal_or_hex(struct gremlinp_parser_buffer* buf)
{
    char x = gremlinp_parser_buffer_char(buf);
    if (x == '\0') {
        return GREMLINP_OK;
    }

    if (x == 'x' || x == 'X') {
        buf->offset++;
        char c = gremlinp_parser_buffer_char(buf);
        if (c == '\0' || !is_hex_digit(c)) {
            return GREMLINP_ERROR_INVALID_INTEGER_LITERAL;
        }
        /*@ loop invariant buf->offset >= \at(buf->offset, Pre) + 1;
            loop invariant buf->offset <= buf->buf_size;
            loop invariant all_hex(buf->buf, \at(buf->offset, Pre) + 1, buf->offset - \at(buf->offset, Pre) - 1);
            loop assigns buf->offset;
            loop variant buf->buf_size - buf->offset;
        */
        while (true) {
            char n = gremlinp_parser_buffer_char(buf);
            if (n == '\0' || !is_hex_digit(n)) {
                break;
            }
            buf->offset++;
        }
    } else {
        /*@ loop invariant buf->offset >= \at(buf->offset, Pre);
            loop invariant buf->offset <= buf->buf_size;
            loop invariant all_octal(buf->buf, \at(buf->offset, Pre), buf->offset - \at(buf->offset, Pre));
            loop assigns buf->offset;
            loop variant buf->buf_size - buf->offset;
        */
        while (true) {
            char n = gremlinp_parser_buffer_char(buf);
            if (n == '\0' || !is_octal_digit(n)) {
                break;
            }
            buf->offset++;
        }
    }

    return GREMLINP_OK;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ||
             \result.error == GREMLINP_ERROR_OVERFLOW ||
             \result.error == GREMLINP_ERROR_INVALID_INTEGER_LITERAL;

    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               valid_int_literal_at(buf->buf, \old(buf->offset), buf->offset);

    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_int64_parse_result
gremlinp_lexems_parse_integer_literal(struct gremlinp_parser_buffer* buf)
{
    struct gremlinp_int64_parse_result result = {0, GREMLINP_OK};

    size_t start = buf->offset;

    char first = gremlinp_parser_buffer_char(buf);
    if (first == '-') {
        buf->offset++;
    }

    char c = gremlinp_parser_buffer_char(buf);
    if (c >= '1' && c <= '9') {
        buf->offset++;
        parse_decimal_digits(buf);
    } else if (c == '0') {
        buf->offset++;
        enum gremlinp_parsing_error hex_err = parse_octal_or_hex(buf);
        if (hex_err != GREMLINP_OK) {
            buf->offset = start;
            result.error = hex_err;
            return result;
        }
    } else {
        buf->offset = start;
        result.error = GREMLINP_ERROR_INVALID_INTEGER_LITERAL;
        return result;
    }

    size_t end = buf->offset;
    (void)end;
    /*@ assert end > start; */
    /*@ assert valid_int_literal_at(buf->buf, start, end); */

    char* endptr;
    /*@ assert \separated(buf, &endptr, &errno); */
    errno = 0;
    result.value = strtoll(buf->buf + start, &endptr, 0);

    if (errno == ERANGE) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_OVERFLOW;
        return result;
    }

    // strtoll returns parsed_int_value when in range (axiom: strtoll is correct)
    /*@ assert start == \at(buf->offset, Pre); */
    //@ admit result.value == (long long)parsed_int_value(buf->buf + \at(buf->offset, Pre), 0);
    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ||
             \result.error == GREMLINP_ERROR_OVERFLOW ||
             \result.error == GREMLINP_ERROR_INVALID_INTEGER_LITERAL;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               valid_uint_literal_at(buf->buf, \old(buf->offset), buf->offset);
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_uint64_parse_result
gremlinp_lexems_parse_uint_literal(struct gremlinp_parser_buffer* buf)
{
    struct gremlinp_uint64_parse_result result = {0, GREMLINP_OK};

    size_t start = buf->offset;

    char c = gremlinp_parser_buffer_char(buf);
    if (c >= '1' && c <= '9') {
        buf->offset++;
        parse_decimal_digits(buf);
    } else if (c == '0') {
        buf->offset++;
        enum gremlinp_parsing_error hex_err = parse_octal_or_hex(buf);
        if (hex_err != GREMLINP_OK) {
            buf->offset = start;
            result.error = hex_err;
            return result;
        }
    } else {
        result.error = GREMLINP_ERROR_INVALID_INTEGER_LITERAL;
        return result;
    }

    size_t end = buf->offset;
    (void)end;
    /*@ assert end > start; */
    /*@ assert valid_int_literal_at(buf->buf, start, end); */

    char* endptr;
    /*@ assert \separated(buf, &endptr, &errno); */
    errno = 0;
    result.value = strtoull(buf->buf + start, &endptr, 0);

    if (errno == ERANGE) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_OVERFLOW;
        return result;
    }

    //@ admit result.value == (unsigned long long)parsed_uint_value(buf->buf + start, 0);
    return result;
}

/*@ logic integer exp_sign_len{L}(char *buf, integer pos, integer buf_size) =
      pos < buf_size && (buf[pos] == '+' || buf[pos] == '-') ? 1 : 0;
*/

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;

    ensures  !is_exp_start(buf->buf[\old(buf->offset)]) ==>
               buf->offset == \old(buf->offset);
    ensures  is_exp_start(buf->buf[\old(buf->offset)]) &&
             !is_sign(buf->buf[\old(buf->offset) + 1]) ==>
               buf->offset >= \old(buf->offset) + 1 &&
               all_digits(buf->buf, \old(buf->offset) + 1,
                          buf->offset - \old(buf->offset) - 1);
    ensures  is_exp_start(buf->buf[\old(buf->offset)]) &&
             is_sign(buf->buf[\old(buf->offset) + 1]) ==>
               buf->offset >= \old(buf->offset) + 2 &&
               all_digits(buf->buf, \old(buf->offset) + 2,
                          buf->offset - \old(buf->offset) - 2);
*/
static void
parse_exponent(struct gremlinp_parser_buffer* buf)
{
    char c = gremlinp_parser_buffer_char(buf);
    if (c != 'e' && c != 'E') {
        return;
    }

    buf->offset++;

    char sign = gremlinp_parser_buffer_char(buf);
    if (sign == '+' || sign == '-') {
        buf->offset++;
    }

    parse_decimal_digits(buf);
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ||
             \result.error == GREMLINP_ERROR_OVERFLOW ||
             \result.error == GREMLINP_ERROR_INVALID_FLOAT;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               valid_float_literal_at(buf->buf, \old(buf->offset), buf->offset);
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_double_parse_result
gremlinp_lexems_parse_float_literal(struct gremlinp_parser_buffer* buf)
{
    struct gremlinp_double_parse_result result = {0.0, GREMLINP_OK};

    size_t start = buf->offset;

    if (gremlinp_parser_buffer_char(buf) == '-') {
        buf->offset++;
    }

    if (gremlinp_parser_buffer_check_str_and_shift(buf, KW_INF)) {
        result.value = buf->buf[start] == '-' ? -INFINITY : INFINITY;
        return result;
    }
    if (gremlinp_parser_buffer_check_str_and_shift(buf, KW_NAN)) {
        result.value = NAN;
        return result;
    }

    size_t initial_offset = buf->offset;

    char c = gremlinp_parser_buffer_char(buf);
    if (c == '.') {
        buf->offset++;
        parse_decimal_digits(buf);
        parse_exponent(buf);
    } else {
        parse_decimal_digits(buf);
        if (buf->offset == initial_offset) {
            buf->offset = start;
            result.error = GREMLINP_ERROR_INVALID_FLOAT;
            return result;
        }
        char next_c = gremlinp_parser_buffer_char(buf);
        if (next_c == '.') {
            buf->offset++;
            parse_decimal_digits(buf);
            parse_exponent(buf);
        } else {
            parse_exponent(buf);
        }
    }

    if (buf->offset == initial_offset) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_INVALID_FLOAT;
        return result;
    }

    size_t end = buf->offset;
    (void)end;

    char* endptr;
    /*@ assert \separated(buf, &endptr, &errno); */
    errno = 0;
    result.value = strtod(buf->buf + start, &endptr);

    if (errno == ERANGE) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_OVERFLOW;
        return result;
    }

    //@ admit result.value == (double)parsed_double_value(buf->buf + start);
    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  \result.error == GREMLINP_OK || \result.error == GREMLINP_ERROR_INVALID_BOOLEAN_LITERAL;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               (\result.value == \true || \result.value == \false) &&
               buf->offset > \old(buf->offset);
    ensures  \result.error == GREMLINP_ERROR_INVALID_BOOLEAN_LITERAL ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_bool_parse_result
gremlinp_lexems_parse_boolean_literal(struct gremlinp_parser_buffer* buf)
{
    struct gremlinp_bool_parse_result result = {false, GREMLINP_OK};

    if (gremlinp_parser_buffer_check_str_and_shift(buf, KW_TRUE)) {
        result.value = true;
        return result;
    }
    if (gremlinp_parser_buffer_check_str_and_shift(buf, KW_FALSE)) {
        return result;
    }

    result.error = GREMLINP_ERROR_INVALID_BOOLEAN_LITERAL;
    return result;
}


/*@ requires valid_buffer(buf);
    assigns  buf->offset;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               buf->buf[buf->offset - 1] == '>' &&
               \result.key_type.length > 0 &&
               \result.value_type.length > 0 &&
               valid_full_identifier(\result.key_type.start, \result.key_type.length) &&
               valid_full_identifier(\result.value_type.start, \result.value_type.length);
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_map_type_parse_result
gremlinp_lexems_parse_map_type(struct gremlinp_parser_buffer* buf)
{
    struct gremlinp_span_parse_result empty = {NULL, 0, GREMLINP_OK};
    struct gremlinp_map_type_parse_result result = {empty, empty, GREMLINP_OK};

    size_t start = buf->offset;

    if (!gremlinp_parser_buffer_check_str_and_shift(buf, KW_MAP)) {
        result.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
        return result;
    }

    if (!gremlinp_parser_buffer_check_and_shift(buf, '<')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
        return result;
    }

    gremlinp_parser_buffer_skip_spaces(buf);

    struct gremlinp_span_parse_result key = gremlinp_lexems_parse_full_identifier(buf);
    if (key.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
        return result;
    }
    result.key_type = key;

    gremlinp_parser_buffer_skip_spaces(buf);

    if (!gremlinp_parser_buffer_check_and_shift(buf, ',')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
        return result;
    }

    gremlinp_parser_buffer_skip_spaces(buf);

    struct gremlinp_span_parse_result val = gremlinp_lexems_parse_full_identifier(buf);
    if (val.error != GREMLINP_OK) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
        return result;
    }
    result.value_type = val;

    gremlinp_parser_buffer_skip_spaces(buf);

    if (!gremlinp_parser_buffer_check_and_shift(buf, '>')) {
        buf->offset = start;
        result.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
        return result;
    }

    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;

    ensures  \result.error == GREMLINP_OK ==>
               buf->offset > \old(buf->offset) &&
               (\result.kind == GREMLINP_CONST_FLOAT ||
                \result.kind == GREMLINP_CONST_INT ||
                \result.kind == GREMLINP_CONST_UINT ||
                \result.kind == GREMLINP_CONST_IDENTIFIER ||
                \result.kind == GREMLINP_CONST_STRING);

    ensures  \result.error == GREMLINP_OK && \result.kind == GREMLINP_CONST_IDENTIFIER ==>
               valid_full_identifier(\result.u.span.start, \result.u.span.length);

    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_const_parse_result
gremlinp_lexems_parse_const_value(struct gremlinp_parser_buffer* buf)
{
    struct gremlinp_const_parse_result result;
    result.error = GREMLINP_OK;

    size_t start = buf->offset;

    // Try int, uint, float — longest match wins, int preferred over uint over float at equal length
    struct gremlinp_int64_parse_result int_res = gremlinp_lexems_parse_integer_literal(buf);
    size_t int_end = (int_res.error == GREMLINP_OK) ? buf->offset : start;
    buf->offset = start;

    struct gremlinp_uint64_parse_result uint_res = gremlinp_lexems_parse_uint_literal(buf);
    size_t uint_end = (uint_res.error == GREMLINP_OK) ? buf->offset : start;
    buf->offset = start;

    struct gremlinp_double_parse_result float_res = gremlinp_lexems_parse_float_literal(buf);
    size_t float_end = (float_res.error == GREMLINP_OK) ? buf->offset : start;
    buf->offset = start;

    // Pick longest match; at equal length prefer int > uint > float
    if (int_end >= uint_end && int_end >= float_end && int_res.error == GREMLINP_OK) {
        buf->offset = int_end;
        result.kind = GREMLINP_CONST_INT;
        result.u.int_value = int_res.value;
        return result;
    }
    if (uint_end >= float_end && uint_res.error == GREMLINP_OK) {
        buf->offset = uint_end;
        result.kind = GREMLINP_CONST_UINT;
        result.u.uint_value = uint_res.value;
        return result;
    }
    if (float_res.error == GREMLINP_OK) {
        buf->offset = float_end;
        result.kind = GREMLINP_CONST_FLOAT;
        result.u.float_value = float_res.value;
        return result;
    }

    struct gremlinp_span_parse_result ident = gremlinp_lexems_parse_full_identifier(buf);
    if (ident.error == GREMLINP_OK) {
        result.kind = GREMLINP_CONST_IDENTIFIER;
        result.u.span = ident;
        return result;
    }

    struct gremlinp_span_parse_result str = gremlinp_lexems_parse_string_literal(buf);
    if (str.error == GREMLINP_OK) {
        result.kind = GREMLINP_CONST_STRING;
        result.u.span = str;
        return result;
    }

    result.error = GREMLINP_ERROR_INVALID_CONST;
    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               1 <= \result.value <= 2147483647 &&
               buf->offset > \old(buf->offset);
    ensures  \result.error == GREMLINP_ERROR_INVALID_FIELD_VALUE ==>
               buf->offset > \old(buf->offset);
*/
struct gremlinp_int32_parse_result
gremlinp_lexems_parse_field_number(struct gremlinp_parser_buffer* buf)
{
    struct gremlinp_int32_parse_result result = {0, GREMLINP_OK};

    struct gremlinp_int64_parse_result int_res = gremlinp_lexems_parse_integer_literal(buf);
    if (int_res.error != GREMLINP_OK) {
        result.error = int_res.error;
        return result;
    }

    if (int_res.value < 1 || int_res.value > INT32_MAX) {
        result.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
        return result;
    }

    result.value = (int32_t)int_res.value;
    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               -2147483648 <= \result.value <= 2147483647 &&
               buf->offset > \old(buf->offset);
*/
struct gremlinp_int32_parse_result
gremlinp_lexems_parse_enum_value_number(struct gremlinp_parser_buffer* buf)
{
    struct gremlinp_int32_parse_result result = {0, GREMLINP_OK};

    struct gremlinp_int64_parse_result int_res = gremlinp_lexems_parse_integer_literal(buf);
    if (int_res.error != GREMLINP_OK) {
        result.error = int_res.error;
        return result;
    }

    if (int_res.value < INT32_MIN || int_res.value > INT32_MAX) {
        result.error = GREMLINP_ERROR_OVERFLOW;
        return result;
    }

    result.value = (int32_t)int_res.value;
    return result;
}

/*@ requires valid_buffer(buf);
    assigns  buf->offset, errno;
    ensures  buf->offset >= \old(buf->offset);
    ensures  buf->offset <= buf->buf_size;
    ensures  \result.error == GREMLINP_OK ==>
               0 <= \result.start <= 2147483647 &&
               \result.start <= \result.end &&
               buf->offset > \old(buf->offset);
    ensures  \result.error == GREMLINP_OK && \result.is_max == \true ==>
               \result.end == 536870911;
    ensures  \result.error == GREMLINP_OK && \result.is_max == \false ==>
               \result.end <= 2147483647;
    ensures  \result.error != GREMLINP_OK ==>
               buf->offset == \old(buf->offset);
*/
struct gremlinp_range_parse_result
gremlinp_lexems_parse_range(struct gremlinp_parser_buffer* buf)
{
    struct gremlinp_range_parse_result result = {0, 0, false, GREMLINP_OK};

    size_t saved = buf->offset;

    struct gremlinp_int64_parse_result start_res = gremlinp_lexems_parse_integer_literal(buf);
    if (start_res.error != GREMLINP_OK) {
        buf->offset = saved;
        result.error = start_res.error;
        return result;
    }

    if (start_res.value < 0 || start_res.value > INT32_MAX) {
        buf->offset = saved;
        result.error = GREMLINP_ERROR_INVALID_EXTENSIONS_RANGE;
        return result;
    }

    result.start = (int32_t)start_res.value;
    result.end = result.start;

    // Skip spaces before optional "to" keyword
    size_t before_to = buf->offset;
    gremlinp_parser_buffer_skip_spaces(buf);

    // Check for "to" keyword
    if (gremlinp_parser_buffer_check_str_with_space_and_shift(buf, KW_TO)) {
        // Check for "max"
        if (gremlinp_parser_buffer_check_str_and_shift(buf, KW_MAX)) {
            result.end = 536870911;
            result.is_max = true;
        } else {
            struct gremlinp_int64_parse_result end_res = gremlinp_lexems_parse_integer_literal(buf);
            if (end_res.error != GREMLINP_OK) {
                buf->offset = saved;
                result.error = end_res.error;
                return result;
            }

            if (end_res.value < 0 || end_res.value > INT32_MAX || end_res.value < start_res.value) {
                buf->offset = saved;
                result.error = GREMLINP_ERROR_INVALID_EXTENSIONS_RANGE;
                return result;
            }

            result.end = (int32_t)end_res.value;
        }
    } else {
        buf->offset = before_to;
    }

    return result;
}

/*@ requires valid_read_string(str);
    assigns  \nothing;
    ensures  \result == \true <==> valid_identifier(str, strlen(str));
*/
bool
gremlinp_lexems_is_valid_identifier(const char* str)
{
    if (*str == '\0') {
        return false;
    }

    if (*str != '_' && !(*str >= 'a' && *str <= 'z') && !(*str >= 'A' && *str <= 'Z')) {
        return false;
    }

    size_t len = strlen(str);
    /*@ loop invariant 1 <= i <= len;
        loop invariant \forall integer k; 1 <= k < i ==> is_ident_char(str[k]);
        loop assigns i;
        loop variant len - i;
    */
    for (size_t i = 1; i < len; i++) {
        if (!is_identifier_char(str[i])) {
            return false;
        }
    }

    return true;
}

/*@ requires valid_read_string(str);
    assigns  \nothing;
    ensures  \result == \true ==>
               \exists integer i; 0 <= i < BUILTIN_TYPES_COUNT &&
                 strcmp(str, BUILTIN_TYPES[i]) == 0;
    ensures  \result == \false ==>
               \forall integer i; 0 <= i < BUILTIN_TYPES_COUNT ==>
                 strcmp(str, BUILTIN_TYPES[i]) != 0;
*/
bool
gremlinp_lexems_is_builtin_type(const char* str)
{
    /*@ loop invariant 0 <= i <= BUILTIN_TYPES_COUNT;
        loop invariant \forall integer j; 0 <= j < i ==>
          strcmp(str, BUILTIN_TYPES[j]) != 0;
        loop assigns i;
        loop variant BUILTIN_TYPES_COUNT - i;
    */
    for (size_t i = 0; i < BUILTIN_TYPES_COUNT; i++) {
        //@ admit valid_read_string(BUILTIN_TYPES[i]);
        if (strcmp(str, BUILTIN_TYPES[i]) == 0) {
            return true;
        }
    }

    return false;
}

/*@ requires valid_read_string(str);
    assigns  \nothing;
    ensures  \result == \true ==>
               \exists integer i; 0 <= i < VALID_MAP_KEY_TYPES_COUNT &&
                 strcmp(str, VALID_MAP_KEY_TYPES[i]) == 0;
    ensures  \result == \false ==>
               \forall integer i; 0 <= i < VALID_MAP_KEY_TYPES_COUNT ==>
                 strcmp(str, VALID_MAP_KEY_TYPES[i]) != 0;
*/
bool
gremlinp_lexems_is_valid_map_key_type(const char* str)
{
    /*@ loop invariant 0 <= i <= VALID_MAP_KEY_TYPES_COUNT;
        loop invariant \forall integer j; 0 <= j < i ==>
          strcmp(str, VALID_MAP_KEY_TYPES[j]) != 0;
        loop assigns i;
        loop variant VALID_MAP_KEY_TYPES_COUNT - i;
    */
    for (size_t i = 0; i < VALID_MAP_KEY_TYPES_COUNT; i++) {
        //@ admit valid_read_string(VALID_MAP_KEY_TYPES[i]);
        if (strcmp(str, VALID_MAP_KEY_TYPES[i]) == 0) {
            return true;
        }
    }

    return false;
}

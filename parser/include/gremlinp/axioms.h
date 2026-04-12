#ifndef _GREMLINP_AXIOMS_H_
#define _GREMLINP_AXIOMS_H_

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

struct gremlinp_parser_buffer {
	char				*buf;
	size_t				buf_size;
	size_t				offset;
};

/*@ axiomatic CountNewlines {
      logic integer count_newlines{L}(char *buf, integer from, integer to) =
        from >= to ? 0 :
        (buf[from] == '\n' ? 1 : 0) + count_newlines(buf, from + 1, to);

      // Obvious by definition; Alt-Ergo/Z3 can't prove by induction.
      axiom count_newlines_step{L}:
        \forall char *buf, integer from, integer to;
          from < to ==>
            count_newlines(buf, from, to) ==
              count_newlines(buf, from, to - 1) +
              (buf[to - 1] == '\n' ? 1 : 0);

      axiom count_newlines_non_negative{L}:
        \forall char *buf, integer from, integer to;
          count_newlines(buf, from, to) >= 0;

      axiom count_newlines_bounded{L}:
        \forall char *buf, integer from, integer to;
          from <= to ==>
            count_newlines(buf, from, to) <= to - from;
    }
*/

/*@ logic integer last_newline_before{L}(char *buf, integer pos) =
      pos <= 0 ? 0 :
      buf[pos - 1] == '\n' ? pos :
      last_newline_before(buf, pos - 1);

    logic integer next_newline_after{L}(char *buf, integer pos, integer limit) =
      pos >= limit ? limit :
      buf[pos] == '\n' ? pos :
      next_newline_after(buf, pos + 1, limit);

    predicate is_ws(char c) =
      c == ' ' || c == '\t' || c == '\n' || c == '\r';

    predicate at_comment_start{L}(char *buf, size_t buf_size, size_t off) =
      off + 1 < buf_size &&
      buf[off] == '/' &&
      (buf[off + 1] == '/' || buf[off + 1] == '*');

    predicate at_token{L}(struct gremlinp_parser_buffer *pb) =
      pb->offset >= pb->buf_size ||
      (!is_ws(pb->buf[pb->offset]) &&
       !at_comment_start(pb->buf, pb->buf_size, pb->offset));

    predicate valid_buffer(struct gremlinp_parser_buffer *pb) =
      \valid(pb) &&
      pb->buf_size >= 0 &&
      pb->buf_size <= 0x7FFFFFFE &&
      pb->offset <= pb->buf_size &&
      \valid_read(pb->buf + (0 .. pb->buf_size)) &&
      pb->buf[pb->buf_size] == '\0';

    predicate is_hex(char c) =
      '0' <= c <= '9' || 'a' <= c <= 'f' || 'A' <= c <= 'F';

    predicate is_octal(char c) = '0' <= c <= '7';

    predicate is_digit(char c) = '0' <= c <= '9';

    predicate is_ident_char(char c) =
      '0' <= c <= '9' || 'a' <= c <= 'z' || 'A' <= c <= 'Z' || c == '_';

    predicate is_ident_start(char c) =
      'a' <= c <= 'z' || 'A' <= c <= 'Z' || c == '_';

    predicate all_hex{L}(char *buf, integer from, integer count) =
      \forall integer k; 0 <= k < count ==> is_hex(buf[from + k]);

    predicate all_octal{L}(char *buf, integer from, integer count) =
      \forall integer k; 0 <= k < count ==> is_octal(buf[from + k]);

    predicate all_digits{L}(char *buf, integer from, integer count) =
      \forall integer k; 0 <= k < count ==> is_digit(buf[from + k]);

    predicate valid_identifier{L}(char *str, integer len) =
      len > 0 &&
      is_ident_start(str[0]) &&
      (\forall integer k; 1 <= k < len ==> is_ident_char(str[k]));

    predicate valid_full_identifier{L}(char *str, integer len) =
      len > 0 &&
      (str[0] == '.' || is_ident_start(str[0])) &&
      (str[0] == '.' ==> len >= 2 && is_ident_start(str[1])) &&
      (\forall integer k; 1 <= k < len ==>
        is_ident_char(str[k]) || str[k] == '.');

    predicate is_simple_escape(char c) =
      c == 'a' || c == 'b' || c == 'f' || c == 'n' || c == 'r' ||
      c == 't' || c == 'v' || c == '\\' || c == '\'' || c == '"' || c == '?';

    predicate is_exp_start(char c) = c == 'e' || c == 'E';
    predicate is_sign(char c) = c == '+' || c == '-';

    // unsigned integer literal: decimal, octal, or hex
    predicate valid_uint_literal_at{L}(char *buf, integer start, integer end) =
      start < end &&
      buf[start] >= '0' && buf[start] <= '9' &&
      (  (buf[start] >= '1' && buf[start] <= '9' &&
          all_digits(buf, start + 1, end - start - 1))
      || (buf[start] == '0' && end == start + 1)
      || (buf[start] == '0' && start + 1 < end &&
          all_octal(buf, start + 1, end - start - 1))
      || (buf[start] == '0' && start + 2 < end &&
          (buf[start + 1] == 'x' || buf[start + 1] == 'X') &&
          all_hex(buf, start + 2, end - start - 2)));

    // signed integer literal: optional '-' then unsigned
    predicate valid_int_literal_at{L}(char *buf, integer start, integer end) =
      start < end &&
      (  valid_uint_literal_at(buf, start, end)
      || (buf[start] == '-' && valid_uint_literal_at(buf, start + 1, end)));

    // float literal starts with optional '-', then inf/nan/numeric
    predicate valid_float_literal_at{L}(char *buf, integer start, integer end) =
      start < end &&
      (  (buf[start] != '-' && buf[start] == 'i')
      || (buf[start] == '-' && start + 1 < end && buf[start + 1] == 'i')
      || (buf[start] != '-' && buf[start] == 'n')
      || (buf[start] == '-' && start + 1 < end && buf[start + 1] == 'n')
      || (buf[start] == '.' || is_digit(buf[start]))
      || (buf[start] == '-' && start + 1 < end &&
          (buf[start + 1] == '.' || is_digit(buf[start + 1]))));

    // proto syntax version
    predicate is_proto_version{L}(char *s, integer len) =
      len == 6 &&
      s[0] == 'p' && s[1] == 'r' && s[2] == 'o' &&
      s[3] == 't' && s[4] == 'o' &&
      (s[5] == '2' || s[5] == '3');
*/

/*@ axiomatic ParsedValue {
      logic integer parsed_int_value{L}(char *s, integer base)
        reads s[0 .. strlen(s)];

      axiom strtoll_value_correct:
        \forall char *s, integer base;
          valid_read_string(s) ==>
          str_to_integer(s, -9223372036854775808, 9223372036854775807, base) == 1 ==>
          -9223372036854775808 <= parsed_int_value(s, base) <= 9223372036854775807;

      logic integer parsed_uint_value{L}(char *s, integer base)
        reads s[0 .. strlen(s)];

      axiom strtoull_value_correct:
        \forall char *s, integer base;
          valid_read_string(s) ==>
          str_to_integer(s, 0, 18446744073709551615, base) == 1 ==>
          0 <= parsed_uint_value(s, base) <= 18446744073709551615;

      logic real parsed_double_value{L}(char *s)
        reads s[0 .. strlen(s)];
    }
*/

#endif /* !_GREMLINP_AXIOMS_H_ */

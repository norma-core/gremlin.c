#include <string.h>
#include "gremlinp/buffer.h"
#include "gremlinp/errors.h"

static const char WHITESPACE_CHARS[] = {' ', '\t', '\n', '\r'};
static const size_t WHITESPACE_COUNT = sizeof(WHITESPACE_CHARS) / sizeof(WHITESPACE_CHARS[0]);

/*@ assigns \nothing;
    ensures \result == \true <==> is_ws(c);
*/
static bool is_whitespace(char c)
{
	/*@ loop invariant 0 <= i <= WHITESPACE_COUNT;
	    loop invariant \forall size_t j; 0 <= j < i ==> WHITESPACE_CHARS[j] != c;
	    loop assigns i;
	    loop variant WHITESPACE_COUNT - i;
	*/
	for (size_t i = 0; i < WHITESPACE_COUNT; i++) {
		if (c == WHITESPACE_CHARS[i]) {
			return true;
		}
	}
	return false;
}

/*@ requires \valid(pb);
    requires valid_read_string(buf);
    requires strlen(buf) <= 0x7FFFFFFE;
    requires offset <= strlen(buf);
    assigns  *pb;
    ensures  valid_buffer(pb);
    ensures  pb->buf == buf;
    ensures  pb->buf_size == strlen{Pre}(buf);
    ensures  pb->offset == offset;
*/
void
gremlinp_parser_buffer_init(struct gremlinp_parser_buffer *pb, char *buf, size_t offset)
{
	pb->buf = buf;
	pb->buf_size = strlen(buf);
	pb->offset = offset;

}

/*@ requires valid_buffer(pb);
    requires valid_read_string(prefix);
    requires prefix[0] != '\0';
    assigns  pb->offset \from pb->offset;
    ensures  \result == \true  ==> pb->offset > \old(pb->offset);
    ensures  \result == \false ==> pb->offset == \old(pb->offset);
    ensures  pb->offset <= pb->buf_size;
*/
bool
gremlinp_parser_buffer_check_str_and_shift(struct gremlinp_parser_buffer *pb, const char *prefix)
{
	size_t prefix_len = strlen(prefix);
	if (pb->offset + prefix_len > pb->buf_size) {
		return false;
	}

	if (strncmp(pb->buf + pb->offset, prefix, prefix_len) == 0) {
		pb->offset += prefix_len;
		return true;
	}

	return false;
}

/*@ requires valid_buffer(pb);
    requires valid_read_string(prefix);
    requires prefix[0] != '\0';
    assigns  pb->offset \from pb->offset;
    ensures  \result == \true  ==> pb->offset > \old(pb->offset);
    ensures  \result == \false ==> pb->offset == \old(pb->offset);
    ensures  pb->offset <= pb->buf_size;
*/
bool
gremlinp_parser_buffer_check_str_with_space_and_shift(struct gremlinp_parser_buffer *pb, const char *prefix)
{
	size_t prefix_len = strlen(prefix);
	if (pb->offset + prefix_len >= pb->buf_size) {
		return false;
	}

	if (strncmp(pb->buf + pb->offset, prefix, prefix_len) != 0) {
		return false;
	}
	
	if (is_whitespace(pb->buf[pb->offset + prefix_len])) {
		pb->offset += prefix_len + 1;
		return true;
	}
	
	return false;
}

/*@ requires valid_buffer(pb);
    assigns  pb->offset \from pb->offset, pb->buf[0 .. pb->buf_size];
    ensures  pb->offset <= pb->buf_size;
    ensures  pb->offset >= \old(pb->offset);
    ensures  \result == GREMLINP_OK || \result == GREMLINP_ERROR_UNEXPECTED_EOF;
    ensures  \result == GREMLINP_OK ==> at_token(pb);
*/
enum gremlinp_parsing_error
gremlinp_parser_buffer_skip_spaces(struct gremlinp_parser_buffer *pb)
{
	/*@ loop invariant pb->offset <= pb->buf_size;
	    loop invariant pb->offset >= \at(pb->offset, Pre);
	    loop assigns pb->offset;
	    loop variant pb->buf_size - pb->offset;
	*/
	while (pb->offset < pb->buf_size) {
		if (is_whitespace(pb->buf[pb->offset])) {
			pb->offset++;
			continue;
		}

		if (pb->buf[pb->offset] != '/') {
			return GREMLINP_OK;
		}

		if (pb->offset + 1 >= pb->buf_size) {
			return GREMLINP_ERROR_UNEXPECTED_EOF;
		}

		size_t saved = pb->offset;
		(void)saved;
		if (pb->buf[pb->offset + 1] == '/') {
			pb->offset += 2;
			/*@ loop invariant pb->offset <= pb->buf_size;
			    loop invariant pb->offset > saved;
			    loop invariant pb->offset >= \at(pb->offset, Pre);
			    loop assigns pb->offset;
			    loop variant pb->buf_size - pb->offset;
			*/
			while (pb->offset < pb->buf_size && pb->buf[pb->offset] != '\n') {
				pb->offset++;
			}
			/*@ assert pb->offset > saved; */
		} else if (pb->buf[pb->offset + 1] == '*') {
			pb->offset += 2;
			/*@ loop invariant pb->offset <= pb->buf_size;
			    loop invariant pb->offset > saved;
			    loop invariant pb->offset >= \at(pb->offset, Pre);
			    loop assigns pb->offset;
			    loop variant pb->buf_size - pb->offset;
			*/
			while (true) {
				if (pb->offset >= pb->buf_size) {
					return GREMLINP_ERROR_UNEXPECTED_EOF;
				}
				if (pb->offset + 1 < pb->buf_size &&
					pb->buf[pb->offset] == '*' &&
					pb->buf[pb->offset + 1] == '/') {
					pb->offset += 2;
					break;
				}
				pb->offset++;
			}
			/*@ assert pb->offset > saved; */
		} else {
			return GREMLINP_OK;
		}
	}

	return GREMLINP_OK;
}

/*@ requires valid_buffer(pb);
    assigns  pb->offset \from pb->offset;
    ensures  \result == \true <==>
               \old(pb->offset) < pb->buf_size &&
               pb->buf[\old(pb->offset)] == expected;
    ensures  \result == \true  ==> pb->offset == \old(pb->offset) + 1;
    ensures  \result == \false ==> pb->offset == \old(pb->offset);
    ensures  pb->offset <= pb->buf_size;
*/
bool
gremlinp_parser_buffer_check_and_shift(struct gremlinp_parser_buffer *pb, char expected)
{
	if (pb->offset >= pb->buf_size) {
		return false;
	}

	if (pb->buf[pb->offset] == expected) {
		pb->offset++;
		return true;
	}

	return false;
}

/*@ requires valid_buffer(pb);
    assigns  \nothing;
    ensures  pb->offset >= pb->buf_size ==> \result == '\0';
    ensures  pb->offset < pb->buf_size  ==> \result == pb->buf[pb->offset];
*/
char
gremlinp_parser_buffer_char(const struct gremlinp_parser_buffer *pb)
{
	if (pb->offset >= pb->buf_size) {
		return '\0';
	}

	return pb->buf[pb->offset];
}

/*@ requires valid_buffer(pb);
    assigns  pb->offset \from pb->offset;
    ensures  \old(pb->offset) >= pb->buf_size ==>
               \result == '\0' && pb->offset == \old(pb->offset);
    ensures  \old(pb->offset) < pb->buf_size
               ==> \result == pb->buf[\old(pb->offset)]
               &&  pb->offset == \old(pb->offset) + 1;
    ensures  pb->offset >= \old(pb->offset);
    ensures  pb->offset <= pb->buf_size;
*/
char
gremlinp_parser_buffer_should_shift_next(struct gremlinp_parser_buffer *pb)
{
	if (pb->offset >= pb->buf_size) {
		return '\0';
	}

	char c = pb->buf[pb->offset];
	pb->offset++;
	return c;
}

/*@ requires valid_buffer(pb);
    requires at_token(pb);
    assigns  pb->offset \from pb->offset;
    ensures  pb->offset <= pb->buf_size;
    ensures  \result == GREMLINP_OK || \result == GREMLINP_ERROR_SEMICOLON_EXPECTED;
    ensures  \result == GREMLINP_OK <==>
               \old(pb->offset) < pb->buf_size &&
               pb->buf[\old(pb->offset)] == ';';
    ensures  \result == GREMLINP_OK ==> pb->offset == \old(pb->offset) + 1;
    ensures  \result != GREMLINP_OK ==> pb->offset == \old(pb->offset);
*/
enum gremlinp_parsing_error
gremlinp_parser_buffer_semicolon(struct gremlinp_parser_buffer *pb)
{
	if (gremlinp_parser_buffer_check_and_shift(pb, ';')) {
		return GREMLINP_OK;
	}

	return GREMLINP_ERROR_SEMICOLON_EXPECTED;
}

/*@ requires valid_buffer(pb);
    requires at_token(pb);
    assigns  pb->offset \from pb->offset;
    ensures  pb->offset <= pb->buf_size;
    ensures  \result == GREMLINP_OK || \result == GREMLINP_ERROR_ASSIGNMENT_EXPECTED;
    ensures  \result == GREMLINP_OK <==>
               \old(pb->offset) < pb->buf_size &&
               pb->buf[\old(pb->offset)] == '=';
    ensures  \result == GREMLINP_OK ==> pb->offset == \old(pb->offset) + 1;
    ensures  \result != GREMLINP_OK ==> pb->offset == \old(pb->offset);
*/
enum gremlinp_parsing_error
gremlinp_parser_buffer_assignment(struct gremlinp_parser_buffer *pb)
{
	if (gremlinp_parser_buffer_check_and_shift(pb, '=')) {
		return GREMLINP_OK;
	}

	return GREMLINP_ERROR_ASSIGNMENT_EXPECTED;
}

/*@ requires valid_buffer(pb);
    requires at_token(pb);
    assigns  pb->offset \from pb->offset;
    ensures  pb->offset <= pb->buf_size;
    ensures  \result == GREMLINP_OK || \result == GREMLINP_ERROR_BRACKET_EXPECTED;
    ensures  \result == GREMLINP_OK <==>
               \old(pb->offset) < pb->buf_size &&
               pb->buf[\old(pb->offset)] == '{';
    ensures  \result == GREMLINP_OK ==> pb->offset == \old(pb->offset) + 1;
    ensures  \result != GREMLINP_OK ==> pb->offset == \old(pb->offset);
*/
enum gremlinp_parsing_error
gremlinp_parser_buffer_open_bracket(struct gremlinp_parser_buffer *pb)
{
	if (gremlinp_parser_buffer_check_and_shift(pb, '{')) {
		return GREMLINP_OK;
	}

	return GREMLINP_ERROR_BRACKET_EXPECTED;
}

/*@ requires valid_buffer(pb);
    requires at_token(pb);
    assigns  pb->offset \from pb->offset;
    ensures  pb->offset <= pb->buf_size;
    ensures  \result == GREMLINP_OK || \result == GREMLINP_ERROR_BRACKET_EXPECTED;
    ensures  \result == GREMLINP_OK <==>
               \old(pb->offset) < pb->buf_size &&
               pb->buf[\old(pb->offset)] == '}';
    ensures  \result == GREMLINP_OK ==> pb->offset == \old(pb->offset) + 1;
    ensures  \result != GREMLINP_OK ==> pb->offset == \old(pb->offset);
*/
enum gremlinp_parsing_error
gremlinp_parser_buffer_close_bracket(struct gremlinp_parser_buffer *pb)
{
	if (gremlinp_parser_buffer_check_and_shift(pb, '}')) {
		return GREMLINP_OK;
	}

	return GREMLINP_ERROR_BRACKET_EXPECTED;
}

/*@ requires valid_buffer(pb);
    assigns  \nothing;
    ensures  \result == 1 + count_newlines(pb->buf, 0, pb->offset);
*/
int
gremlinp_parser_buffer_calc_line_number(const struct gremlinp_parser_buffer *pb)
{
	int line_count = 1;
	/*@ loop invariant 0 <= i <= pb->offset;
	    loop invariant line_count == 1 + count_newlines(pb->buf, 0, i);
	    loop assigns i, line_count;
	    loop variant pb->offset - i;
	*/
	for (size_t i = 0; i < pb->offset; i++) {
		if (pb->buf[i] == '\n') {
			line_count++;
		}
	}

	return line_count;
}

/*@ requires valid_buffer(pb);
    assigns  \nothing;
    ensures  \result == (int)(pb->offset - last_newline_before(pb->buf, pb->offset));
*/
int
gremlinp_parser_buffer_calc_line_start(const struct gremlinp_parser_buffer *pb)
{
	if (pb->offset == 0) {
		return 0;
	}

	size_t last_newline = pb->offset;
	/*@ loop invariant 0 <= last_newline <= pb->offset;
	    loop invariant last_newline <= pb->buf_size;
	    loop invariant last_newline_before(pb->buf, last_newline) == last_newline_before(pb->buf, pb->offset);
	    loop assigns last_newline;
	    loop variant last_newline;
	*/
	while (last_newline > 0 && pb->buf[last_newline - 1] != '\n') {
		last_newline--;
	}

	if (last_newline == 0) {
		return (int)pb->offset;
	}

	return (int)(pb->offset - last_newline);
}

/*@ requires valid_buffer(pb);
    assigns  \nothing;
    ensures  \result == (int)(next_newline_after(pb->buf, pb->offset, pb->buf_size) - pb->offset);
*/
int
gremlinp_parser_buffer_calc_line_end(const struct gremlinp_parser_buffer *pb)
{

	size_t next_newline = pb->offset;
	/*@ loop invariant pb->offset <= next_newline <= pb->buf_size;
	    loop invariant next_newline_after(pb->buf, next_newline, pb->buf_size) == next_newline_after(pb->buf, pb->offset, pb->buf_size);
	    loop assigns next_newline;
	    loop variant pb->buf_size - next_newline;
	*/
	while (next_newline < pb->buf_size && pb->buf[next_newline] != '\n') {
		next_newline++;
	}

	return (int)(next_newline - pb->offset);
}


#ifndef _GREMLINP_BUFFER_H_
#define	_GREMLINP_BUFFER_H_

#include "errors.h"
#include "axioms.h"

/*
 *               .'\   /`.
 *             .'.-.`-'.-.`.
 *        ..._:   .-. .-.   :_...
 *      .'    '-.(o ) (o ).-'    `.
 *     :  _    _ _`~(_)~`_ _    _  :
 *    :  /:   ' .-=_   _=-. `   ;\  :
 *    :   :|-.._  '     `  _..-|:   :
 *     :   `:| |`:-:-.-:-:'| |:'   :
 *      `.   `.| | | | | | |.'   .'
 *        `.   `-:_| | |_:-'   .'
 *          `-._   ````    _.-'
 *              ``-------''
 *
 * Created by ab, 12.04.2026
 */

void					gremlinp_parser_buffer_init(struct gremlinp_parser_buffer *pb, char *buf, size_t offset);
bool					gremlinp_parser_buffer_check_str_and_shift(struct gremlinp_parser_buffer *pb, const char *prefix, size_t prefix_len);
bool					gremlinp_parser_buffer_check_str_with_space_and_shift(struct gremlinp_parser_buffer *pb, const char *prefix, size_t prefix_len);
enum gremlinp_parsing_error		gremlinp_parser_buffer_skip_spaces(struct gremlinp_parser_buffer *pb);
bool					gremlinp_parser_buffer_check_and_shift(struct gremlinp_parser_buffer *pb, char expected);
char					gremlinp_parser_buffer_char(const struct gremlinp_parser_buffer *pb);
char					gremlinp_parser_buffer_should_shift_next(struct gremlinp_parser_buffer *pb);
enum gremlinp_parsing_error		gremlinp_parser_buffer_semicolon(struct gremlinp_parser_buffer *pb);
enum gremlinp_parsing_error		gremlinp_parser_buffer_assignment(struct gremlinp_parser_buffer *pb);
enum gremlinp_parsing_error		gremlinp_parser_buffer_open_bracket(struct gremlinp_parser_buffer *pb);
enum gremlinp_parsing_error		gremlinp_parser_buffer_close_bracket(struct gremlinp_parser_buffer *pb);
int					gremlinp_parser_buffer_calc_line_number(const struct gremlinp_parser_buffer *pb);
int					gremlinp_parser_buffer_calc_line_start(const struct gremlinp_parser_buffer *pb);
int					gremlinp_parser_buffer_calc_line_end(const struct gremlinp_parser_buffer *pb);

#endif /* !_GREMLINP_BUFFER_H_ */

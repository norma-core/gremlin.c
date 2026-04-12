#ifndef _GREMLINP_LEXEMS_H_
#define	_GREMLINP_LEXEMS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "buffer.h"
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

struct gremlinp_span_parse_result		    gremlinp_lexems_parse_string_literal(struct gremlinp_parser_buffer *buf);
struct gremlinp_span_parse_result		    gremlinp_lexems_parse_identifier(struct gremlinp_parser_buffer *buf);
struct gremlinp_span_parse_result		gremlinp_lexems_parse_full_identifier(struct gremlinp_parser_buffer *buf);
struct gremlinp_int64_parse_result		    gremlinp_lexems_parse_integer_literal(struct gremlinp_parser_buffer *buf);
struct gremlinp_uint64_parse_result		    gremlinp_lexems_parse_uint_literal(struct gremlinp_parser_buffer *buf);
struct gremlinp_double_parse_result		    gremlinp_lexems_parse_float_literal(struct gremlinp_parser_buffer *buf);
struct gremlinp_bool_parse_result		    gremlinp_lexems_parse_boolean_literal(struct gremlinp_parser_buffer *buf);


struct gremlinp_map_type_parse_result {
	struct gremlinp_span_parse_result key_type;
	struct gremlinp_span_parse_result value_type;
	enum gremlinp_parsing_error	error;
};
struct gremlinp_map_type_parse_result		gremlinp_lexems_parse_map_type(struct gremlinp_parser_buffer *buf);

enum gremlinp_const_kind {
	GREMLINP_CONST_FLOAT,
	GREMLINP_CONST_INT,
	GREMLINP_CONST_UINT,
	GREMLINP_CONST_IDENTIFIER,
	GREMLINP_CONST_STRING
};

struct gremlinp_const_parse_result {
	enum gremlinp_const_kind	kind;
	union {
		double			float_value;
		int64_t			int_value;
		uint64_t		uint_value;
		struct gremlinp_span_parse_result span;
	} u;
	enum gremlinp_parsing_error	error;
};
struct gremlinp_const_parse_result		gremlinp_lexems_parse_const_value(struct gremlinp_parser_buffer *buf);
struct gremlinp_int32_parse_result		    gremlinp_lexems_parse_field_number(struct gremlinp_parser_buffer *buf);
struct gremlinp_int32_parse_result		    gremlinp_lexems_parse_enum_value_number(struct gremlinp_parser_buffer *buf);

struct gremlinp_range_parse_result {
	int32_t				start;
	int32_t				end;
	bool				is_max;
	enum gremlinp_parsing_error	error;
};
struct gremlinp_range_parse_result		gremlinp_lexems_parse_range(struct gremlinp_parser_buffer *buf);

bool						gremlinp_lexems_is_valid_identifier(const char *str);
bool						gremlinp_lexems_is_builtin_type(const char *str);
bool						gremlinp_lexems_is_valid_map_key_type(const char *str);

#endif /* !_GREMLINP_LEXEMS_H_ */

#ifndef _GREMLINP_ERRORS_H_
#define	_GREMLINP_ERRORS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

enum gremlinp_parsing_error {
	GREMLINP_OK = 0,

	/* Syntax and Structure Errors */
	GREMLINP_ERROR_INVALID_SYNTAX_DEF,
	GREMLINP_ERROR_UNEXPECTED_EOF,
	GREMLINP_ERROR_SPACE_REQUIRED,
	GREMLINP_ERROR_INVALID_SYNTAX_VERSION,
	GREMLINP_ERROR_UNEXPECTED_TOKEN,
	GREMLINP_ERROR_PACKAGE_ALREADY_DEFINED,
	GREMLINP_ERROR_EDITION_ALREADY_DEFINED,

	/* String Parsing Errors */
	GREMLINP_ERROR_INVALID_STRING_LITERAL,
	GREMLINP_ERROR_INVALID_UNICODE_ESCAPE,
	GREMLINP_ERROR_INVALID_ESCAPE,

	/* Syntax Element Errors */
	GREMLINP_ERROR_SEMICOLON_EXPECTED,
	GREMLINP_ERROR_ASSIGNMENT_EXPECTED,
	GREMLINP_ERROR_BRACKET_EXPECTED,

	/* Identifier and Name Errors */
	GREMLINP_ERROR_IDENTIFIER_SHOULD_START_WITH_LETTER,
	GREMLINP_ERROR_INVALID_OPTION_NAME,
	GREMLINP_ERROR_INVALID_FIELD_NAME,

	/* Value and Type Errors */
	GREMLINP_ERROR_OPTION_VALUE_REQUIRED,
	GREMLINP_ERROR_INVALID_INTEGER_LITERAL,
	GREMLINP_ERROR_INVALID_BOOLEAN_LITERAL,
	GREMLINP_ERROR_INVALID_CONST,
	GREMLINP_ERROR_INVALID_FLOAT,
	GREMLINP_ERROR_INVALID_FIELD_VALUE,
	GREMLINP_ERROR_INVALID_MAP_KEY_TYPE,
	GREMLINP_ERROR_INVALID_MAP_VALUE_TYPE,

	/* Definition Errors */
	GREMLINP_ERROR_INVALID_ENUM_DEF,
	GREMLINP_ERROR_INVALID_ONEOF_ELEMENT,
	GREMLINP_ERROR_INVALID_EXTENSIONS_RANGE,

	/* Reference and Resolution Errors */
	GREMLINP_ERROR_EXTEND_SOURCE_NOT_FOUND,
	GREMLINP_ERROR_TYPE_NOT_FOUND,
	GREMLINP_ERROR_IMPORT_TARGET_NOT_FOUND,
	GREMLINP_ERROR_CIRCULAR_IMPORT,
	GREMLINP_ERROR_IMPORT_PATH_INVALID,

	/* System and Runtime Errors */
	GREMLINP_ERROR_OVERFLOW,
	GREMLINP_ERROR_INVALID_CHARACTER,
	GREMLINP_ERROR_OUT_OF_MEMORY,

	/* Additional C-specific errors */
	GREMLINP_ERROR_NULL_POINTER,
	GREMLINP_ERROR_BUFFER_OVERFLOW
};

const char	*gremlinp_parsing_error_to_string(enum gremlinp_parsing_error error);

struct gremlinp_string_parse_result {
	char			*value;
	enum gremlinp_parsing_error error;
};

struct gremlinp_scoped_name_parse_result {
	struct gremlinp_scoped_name	*value;
	enum gremlinp_parsing_error error;
};

struct gremlinp_string_array_parse_result {
	char			**value;
	size_t			count;
	enum gremlinp_parsing_error error;
};

struct gremlinp_int64_parse_result {
	int64_t			value;
	enum gremlinp_parsing_error error;
};

struct gremlinp_uint64_parse_result {
	uint64_t		value;
	enum gremlinp_parsing_error error;
};

struct gremlinp_double_parse_result {
	double			value;
	enum gremlinp_parsing_error error;
};

struct gremlinp_bool_parse_result {
	bool			value;
	enum gremlinp_parsing_error error;
};

struct gremlinp_span_parse_result {
	const char		*start;
	size_t			length;
	enum gremlinp_parsing_error error;
};

struct gremlinp_int32_parse_result {
	int32_t			value;
	enum gremlinp_parsing_error error;
};

#endif /* !_GREMLINP_ERRORS_H_ */

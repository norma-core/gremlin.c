#include "gremlinp/errors.h"

/*@ assigns \nothing;
    ensures \valid_read(\result);
    ensures \result != \null;
*/
const char*
gremlinp_parsing_error_to_string(enum gremlinp_parsing_error error)
{

	switch (error) {
	case GREMLINP_OK:
		return ("Success");

	/* Syntax and Structure Errors */
	case GREMLINP_ERROR_INVALID_SYNTAX_DEF:
		return ("Invalid proto syntax definition");
	case GREMLINP_ERROR_UNEXPECTED_EOF:
		return ("Unexpected end of file");
	case GREMLINP_ERROR_SPACE_REQUIRED:
		return ("Whitespace required");
	case GREMLINP_ERROR_INVALID_SYNTAX_VERSION:
		return ("Invalid or unsupported syntax version");
	case GREMLINP_ERROR_UNEXPECTED_TOKEN:
		return ("Unexpected token");
	case GREMLINP_ERROR_PACKAGE_ALREADY_DEFINED:
		return ("Package already defined");
	case GREMLINP_ERROR_EDITION_ALREADY_DEFINED:
		return ("Edition already defined");

	/* String Parsing Errors */
	case GREMLINP_ERROR_INVALID_STRING_LITERAL:
		return ("Invalid string literal format");
	case GREMLINP_ERROR_INVALID_UNICODE_ESCAPE:
		return ("Invalid Unicode escape sequence");
	case GREMLINP_ERROR_INVALID_ESCAPE:
		return ("Invalid escape sequence");

	/* Syntax Element Errors */
	case GREMLINP_ERROR_SEMICOLON_EXPECTED:
		return ("Semicolon expected");
	case GREMLINP_ERROR_ASSIGNMENT_EXPECTED:
		return ("Assignment operator expected");
	case GREMLINP_ERROR_BRACKET_EXPECTED:
		return ("Bracket expected");

	/* Identifier and Name Errors */
	case GREMLINP_ERROR_IDENTIFIER_SHOULD_START_WITH_LETTER:
		return ("Identifier must start with a letter");
	case GREMLINP_ERROR_INVALID_OPTION_NAME:
		return ("Invalid option name format");
	case GREMLINP_ERROR_INVALID_FIELD_NAME:
		return ("Invalid field name format");

	/* Value and Type Errors */
	case GREMLINP_ERROR_OPTION_VALUE_REQUIRED:
		return ("Option value required");
	case GREMLINP_ERROR_INVALID_INTEGER_LITERAL:
		return ("Invalid integer literal format");
	case GREMLINP_ERROR_INVALID_BOOLEAN_LITERAL:
		return ("Invalid boolean literal format");
	case GREMLINP_ERROR_INVALID_CONST:
		return ("Invalid constant value");
	case GREMLINP_ERROR_INVALID_FLOAT:
		return ("Invalid floating point number format");
	case GREMLINP_ERROR_INVALID_FIELD_VALUE:
		return ("Invalid field value");
	case GREMLINP_ERROR_INVALID_MAP_KEY_TYPE:
		return ("Invalid map key type");
	case GREMLINP_ERROR_INVALID_MAP_VALUE_TYPE:
		return ("Invalid map value type");

	/* Definition Errors */
	case GREMLINP_ERROR_INVALID_ENUM_DEF:
		return ("Invalid enum definition");
	case GREMLINP_ERROR_INVALID_ONEOF_ELEMENT:
		return ("Invalid oneof element");
	case GREMLINP_ERROR_INVALID_EXTENSIONS_RANGE:
		return ("Invalid extensions range specification");

	/* Reference and Resolution Errors */
	case GREMLINP_ERROR_EXTEND_SOURCE_NOT_FOUND:
		return ("Extend source type not found");
	case GREMLINP_ERROR_TYPE_NOT_FOUND:
		return ("Type not found");
	case GREMLINP_ERROR_IMPORT_TARGET_NOT_FOUND:
		return ("Import target file not found");
	case GREMLINP_ERROR_CIRCULAR_IMPORT:
		return ("Circular import detected");
	case GREMLINP_ERROR_IMPORT_PATH_INVALID:
		return ("Invalid import path");

	/* System and Runtime Errors */
	case GREMLINP_ERROR_OVERFLOW:
		return ("Numeric overflow");
	case GREMLINP_ERROR_INVALID_CHARACTER:
		return ("Invalid character");
	case GREMLINP_ERROR_OUT_OF_MEMORY:
		return ("Out of memory");

	/* Feature Support */
	case GREMLINP_ERROR_FEATURE_NOT_SUPPORTED:
		return ("Feature not supported");

	/* Additional C-specific errors */
	case GREMLINP_ERROR_NULL_POINTER:
		return ("NULL pointer");
	case GREMLINP_ERROR_BUFFER_OVERFLOW:
		return ("Buffer overflow");

	default:
		return ("Unknown error");
	}
}

#ifndef _GREMLINP_ENTRIES_H_
#define _GREMLINP_ENTRIES_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "buffer.h"
#include "errors.h"
#include "lexems.h"

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

/* ========================================================================
 * PROTO FILE STRUCTURE
 * ======================================================================== */

/* Proto file structure */
enum gremlinp_file_entry_kind {
	GREMLINP_FILE_ENTRY_SYNTAX = 0,
	GREMLINP_FILE_ENTRY_EDITION,
	GREMLINP_FILE_ENTRY_PACKAGE,
	GREMLINP_FILE_ENTRY_IMPORT,
	GREMLINP_FILE_ENTRY_OPTION,
	GREMLINP_FILE_ENTRY_MESSAGE,
	GREMLINP_FILE_ENTRY_ENUM,
	GREMLINP_FILE_ENTRY_SERVICE,
	GREMLINP_FILE_ENTRY_EXTEND
};
/* struct gremlinp_file_entry_result is defined after all member parse result
 * types below, since its union holds them. */

/* ========================================================================
 * SYNTAX DECLARATION
 * ======================================================================== */

/*
 * Represents a syntax declaration in a protobuf file.
 * Format: syntax = "proto2"|"proto3";
 */
struct gremlinp_syntax_parse_result {
	const char			*version_start;
	size_t				version_length;
	size_t				start;
	size_t				end;
	enum gremlinp_parsing_error	error;
};

struct gremlinp_syntax_parse_result	gremlinp_syntax_parse(struct gremlinp_parser_buffer *buf);

/* ========================================================================
 * EDITION DECLARATION
 * ======================================================================== */

struct gremlinp_edition_parse_result {
	const char			*edition_start;
	size_t				edition_length;
	size_t				start;
	size_t				end;
	enum gremlinp_parsing_error	error;
};

struct gremlinp_edition_parse_result	gremlinp_edition_parse(struct gremlinp_parser_buffer *buf);

/* ========================================================================
 * PACKAGE DECLARATION
 * ======================================================================== */

/*
 * Represents a package declaration in a protobuf file.
 * Format:
 *   package foo.bar.baz;
 */
struct gremlinp_package_parse_result {
	const char			*name_start;
	size_t				name_length;
	size_t				start;
	size_t				end;
	enum gremlinp_parsing_error	error;
};

struct gremlinp_package_parse_result	gremlinp_package_parse(struct gremlinp_parser_buffer *buf);

/* ========================================================================
 * IMPORT DECLARATION
 * ======================================================================== */

enum gremlinp_import_type {
	GREMLINP_IMPORT_TYPE_REGULAR = 0,
	GREMLINP_IMPORT_TYPE_WEAK,
	GREMLINP_IMPORT_TYPE_PUBLIC
};

struct gremlinp_import_parse_result {
	const char			*path_start;
	size_t				path_length;
	enum gremlinp_import_type	type;
	size_t				start;
	size_t				end;
	enum gremlinp_parsing_error	error;
};

struct gremlinp_import_parse_result	gremlinp_import_parse(struct gremlinp_parser_buffer *buf);

/* ========================================================================
 * OPTION DECLARATION
 * ======================================================================== */

/*
 * Represents a protobuf option declaration.
 * Options can appear as standalone declarations or in lists.
 * Format:
 * option java_package = "com.example.foo";
 * message Foo {
 *     string name = 1 [(custom) = "value", deprecated = true];
 * }
 */
struct gremlinp_option_parse_result {
	const char			*name_start;
	size_t				name_length;
	struct gremlinp_const_parse_result value;
	size_t				start;
	size_t				end;
	enum gremlinp_parsing_error	error;
};

struct gremlinp_option_parse_result		gremlinp_option_parse(struct gremlinp_parser_buffer *buf);

struct gremlinp_option_list_result {
	size_t				start;
	size_t				end;
	size_t				count;
	enum gremlinp_parsing_error	error;
};

struct gremlinp_option_list_result		gremlinp_option_list_consume(struct gremlinp_parser_buffer *buf);

/*
 * Single "name = value" option item. Consumers iterate over an already-
 * parsed option list span (as recorded in gremlinp_option_list_result)
 * by rewinding a buffer to list.start + 1 (past the `[`) and calling
 * this repeatedly, separated by `,`, until `]`.
 */
struct gremlinp_option_item_result {
	const char				*name_start;
	size_t					name_length;
	struct gremlinp_const_parse_result	value;
	enum gremlinp_parsing_error		error;
};

struct gremlinp_option_item_result		gremlinp_option_item_parse(struct gremlinp_parser_buffer *buf);

/* ========================================================================
 * FIELD TYPE
 * ======================================================================== */

enum gremlinp_field_type_kind {
	GREMLINP_FIELD_TYPE_NAMED = 0,
	GREMLINP_FIELD_TYPE_MAP
};

struct gremlinp_field_type_parse_result {
	enum gremlinp_field_type_kind	kind;
	union {
		struct gremlinp_span_parse_result	named;
		struct gremlinp_map_type_parse_result	map;
	} u;
	size_t				start;
	size_t				end;
	enum gremlinp_parsing_error	error;
};

struct gremlinp_field_type_parse_result	gremlinp_field_type_parse(struct gremlinp_parser_buffer *buf);


/* ========================================================================
 * MESSAGE STRUCTURE
 * ======================================================================== */

/* Message structure */
struct gremlinp_message_parse_result {
	const char			*name_start;
	size_t				name_length;
	size_t				body_start;
	size_t				body_end;
	size_t				start;
	size_t				end;
	enum gremlinp_parsing_error	error;
};

enum gremlinp_message_entry_kind {
	GREMLINP_MSG_ENTRY_FIELD = 0,
	GREMLINP_MSG_ENTRY_ENUM,
	GREMLINP_MSG_ENTRY_MESSAGE,
	GREMLINP_MSG_ENTRY_ONEOF,
	GREMLINP_MSG_ENTRY_OPTION,
	GREMLINP_MSG_ENTRY_EXTENSIONS,
	GREMLINP_MSG_ENTRY_RESERVED,
	GREMLINP_MSG_ENTRY_EXTEND,
	GREMLINP_MSG_ENTRY_GROUP
};
/* struct gremlinp_message_entry_result is defined after all member parse
 * result types below, since its union holds them. */

struct gremlinp_message_parse_result	gremlinp_message_parse(struct gremlinp_parser_buffer *buf);

/* ========================================================================
 * MESSAGE FIELDS
 * ======================================================================== */

/* Normal field in a message */
enum gremlinp_field_label {
	GREMLINP_FIELD_LABEL_NONE = 0,
	GREMLINP_FIELD_LABEL_OPTIONAL,
	GREMLINP_FIELD_LABEL_REQUIRED,
	GREMLINP_FIELD_LABEL_REPEATED
};

struct gremlinp_field_parse_result {
	enum gremlinp_field_label		label;
	struct gremlinp_field_type_parse_result	type;
	const char				*name_start;
	size_t					name_length;
	int32_t					index;
	struct gremlinp_option_list_result	options;
	size_t					start;
	size_t					end;
	enum gremlinp_parsing_error		error;
};

struct gremlinp_field_parse_result	gremlinp_field_parse(struct gremlinp_parser_buffer *buf);


/* Oneof field: type name = number [options] ; (no label) */
struct gremlinp_oneof_field_parse_result {
	struct gremlinp_span_parse_result	type_name;
	const char				*name_start;
	size_t					name_length;
	int32_t					index;
	struct gremlinp_option_list_result	options;
	size_t					start;
	size_t					end;
	enum gremlinp_parsing_error		error;
};

/* Oneof group: oneof name { fields... } */
struct gremlinp_oneof_parse_result {
	const char			*name_start;
	size_t				name_length;
	size_t				body_start;
	size_t				body_end;
	size_t				start;
	size_t				end;
	enum gremlinp_parsing_error	error;
};

enum gremlinp_oneof_entry_kind {
	GREMLINP_ONEOF_ENTRY_FIELD = 0,
	GREMLINP_ONEOF_ENTRY_OPTION
};

struct gremlinp_oneof_entry_result {
	enum gremlinp_oneof_entry_kind		kind;
	union {
		struct gremlinp_oneof_field_parse_result	field;
		struct gremlinp_option_parse_result		option;
	} u;
	size_t					start;
	size_t					end;
	enum gremlinp_parsing_error		error;
};

struct gremlinp_oneof_field_parse_result	gremlinp_oneof_field_parse(struct gremlinp_parser_buffer *buf);
struct gremlinp_oneof_parse_result		gremlinp_oneof_parse(struct gremlinp_parser_buffer *buf);
struct gremlinp_oneof_entry_result		gremlinp_oneof_next_entry(struct gremlinp_parser_buffer *buf);


/* ========================================================================
 * RESERVED DECLARATION
 * ======================================================================== */

enum gremlinp_reserved_kind {
	GREMLINP_RESERVED_RANGES = 0,
	GREMLINP_RESERVED_NAMES
};

struct gremlinp_reserved_parse_result {
	size_t				start;
	size_t				end;
	size_t				count;
	enum gremlinp_reserved_kind	kind;
	enum gremlinp_parsing_error	error;
};

struct gremlinp_reserved_parse_result	gremlinp_reserved_parse(struct gremlinp_parser_buffer *buf);

/* ========================================================================
 * ENUM STRUCTURE
 * ======================================================================== */

/*
 * Represents a single field within an enum declaration.
 * Example: FOO = 1 [deprecated = true];
 */
struct gremlinp_enum_field_parse_result {
	const char			*name_start;
	size_t				name_length;
	int32_t				index;
	struct gremlinp_option_list_result options;
	size_t				start;
	size_t				end;
	enum gremlinp_parsing_error	error;
};

struct gremlinp_enum_parse_result {
	const char			*name_start;
	size_t				name_length;
	size_t				body_start;
	size_t				body_end;
	size_t				start;
	size_t				end;
	enum gremlinp_parsing_error	error;
};

enum gremlinp_enum_entry_kind {
	GREMLINP_ENUM_ENTRY_FIELD = 0,
	GREMLINP_ENUM_ENTRY_OPTION,
	GREMLINP_ENUM_ENTRY_RESERVED
};

struct gremlinp_enum_entry_result {
	enum gremlinp_enum_entry_kind		kind;
	union {
		struct gremlinp_enum_field_parse_result	field;
		struct gremlinp_option_parse_result	option;
		struct gremlinp_reserved_parse_result	reserved;
	} u;
	size_t					start;
	size_t					end;
	enum gremlinp_parsing_error		error;
};

struct gremlinp_enum_parse_result	gremlinp_enum_parse(struct gremlinp_parser_buffer *buf);
struct gremlinp_enum_field_parse_result	gremlinp_enum_field_parse(struct gremlinp_parser_buffer *buf);
struct gremlinp_enum_entry_result	gremlinp_enum_next_entry(struct gremlinp_parser_buffer *buf);

/* ========================================================================
 * EXTENSIONS DECLARATION
 * ======================================================================== */

/*
 * Extensions represents a proto2 extensions declaration, which defines what field numbers
 * are available for extension fields. The declaration can include individual numbers
 * and ranges (e.g., "extensions 4, 20 to 30;").
 */
struct gremlinp_extensions_parse_result {
	size_t				start;
	size_t				end;
	size_t				count;
	enum gremlinp_parsing_error	error;
};

struct gremlinp_extensions_parse_result	gremlinp_extensions_parse(struct gremlinp_parser_buffer *buf);

/* ========================================================================
 * EXTEND DECLARATION
 * ======================================================================== */

struct gremlinp_extend_parse_result {
	struct gremlinp_span_parse_result base_type;
	size_t				body_start;
	size_t				body_end;
	size_t				start;
	size_t				end;
	enum gremlinp_parsing_error	error;
};

struct gremlinp_extend_parse_result		gremlinp_extend_parse(struct gremlinp_parser_buffer *buf);

/* ========================================================================
 * GROUP DECLARATION
 * ======================================================================== */

struct gremlinp_group_parse_result {
	size_t				start;
	size_t				end;
	enum gremlinp_parsing_error	error;
};

struct gremlinp_group_parse_result	gremlinp_group_parse(struct gremlinp_parser_buffer *buf);

/* ========================================================================
 * SERVICE DECLARATION
 * ======================================================================== */

struct gremlinp_service_parse_result {
	size_t				start;
	size_t				end;
	enum gremlinp_parsing_error	error;
};

struct gremlinp_service_parse_result	gremlinp_service_parse(struct gremlinp_parser_buffer *buf);

/* ========================================================================
 * ITERATOR RESULT TYPES (tagged unions)
 *
 * Defined after all member parse result types because each holds the full
 * parsed payload for the variant returned by the iterator — no re-parsing
 * required.
 * ======================================================================== */

struct gremlinp_file_entry_result {
	enum gremlinp_file_entry_kind		kind;
	union {
		struct gremlinp_syntax_parse_result	syntax;
		struct gremlinp_edition_parse_result	edition;
		struct gremlinp_package_parse_result	package;
		struct gremlinp_import_parse_result	import;
		struct gremlinp_option_parse_result	option;
		struct gremlinp_message_parse_result	message;
		struct gremlinp_enum_parse_result	enumeration;
		struct gremlinp_service_parse_result	service;
		struct gremlinp_extend_parse_result	extend;
	} u;
	size_t					start;
	size_t					end;
	enum gremlinp_parsing_error		error;
};

struct gremlinp_file_entry_result		gremlinp_file_next_entry(struct gremlinp_parser_buffer *buf);

struct gremlinp_message_entry_result {
	enum gremlinp_message_entry_kind	kind;
	union {
		struct gremlinp_field_parse_result	field;
		struct gremlinp_enum_parse_result	enumeration;
		struct gremlinp_message_parse_result	message;
		struct gremlinp_oneof_parse_result	oneof;
		struct gremlinp_option_parse_result	option;
		struct gremlinp_extensions_parse_result	extensions;
		struct gremlinp_reserved_parse_result	reserved;
		struct gremlinp_extend_parse_result	extend;
		struct gremlinp_group_parse_result	group;
	} u;
	size_t					start;
	size_t					end;
	enum gremlinp_parsing_error		error;
};

struct gremlinp_message_entry_result		gremlinp_message_next_entry(struct gremlinp_parser_buffer *buf);

#endif /* !_GREMLINP_ENTRIES_H_ */

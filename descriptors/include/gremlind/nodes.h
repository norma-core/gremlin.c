#ifndef _GREMLIND_NODES_H_
#define _GREMLIND_NODES_H_

#include <stdbool.h>
#include <stddef.h>

#include "name.h"

#include "gremlinp/entries.h"

/*
 * AST node structs. Each wraps a parser result verbatim where the parser
 * already carries everything the AST needs (spans + offsets), and adds
 * only what resolution needs on top (e.g. the resolved_file back-pointer
 * on an import).
 *
 * Spans are pointers into the source buffer. The caller must keep that
 * buffer alive for the AST's lifetime.
 */

/* Optional terminal entries. `present == false` means the .proto file did
 * not declare that entry; value is zero-initialized and must not be read.
 */

struct gremlind_syntax {
	bool					present;
	struct gremlinp_syntax_parse_result	value;
};

struct gremlind_edition {
	bool					present;
	struct gremlinp_edition_parse_result	value;
};

struct gremlind_package {
	bool					present;
	struct gremlinp_package_parse_result	value;
};

/* Forward declaration — an import resolves to another gremlind_file. */
struct gremlind_file;

struct gremlind_import {
	struct gremlinp_import_parse_result	parsed;
	struct gremlind_file			*resolved;	/* NULL until resolve pass */
};

struct gremlind_imports {
	struct gremlind_import			*items;		/* arena-allocated */
	size_t					count;
};

/* Forward decl — enums and fields reference their enclosing message. */
struct gremlind_message;

struct gremlind_enum_value {
	struct gremlinp_enum_field_parse_result	parsed;
};

struct gremlind_enum_values {
	struct gremlind_enum_value		*items;
	size_t					count;
};

struct gremlind_enum {
	struct gremlinp_enum_parse_result	parsed;
	struct gremlind_enum_values		values;
	struct gremlind_message			*parent;	/* NULL if top-level */
	struct gremlind_scoped_name		scoped_name;	/* set by gremlind_compute_scoped_names */
};

struct gremlind_enums {
	struct gremlind_enum			*items;
	size_t					count;
};

/*
 * Resolved type reference for a field. Set by gremlind_resolve_type_refs;
 * UNRESOLVED until then. Every field carries one of these — builtin
 * scalars (int32, string, etc.) just record the name span, named refs
 * point at the target message or enum descriptor, and maps hold two
 * nested type_refs (key always a builtin scalar, value anything).
 */
enum gremlind_type_ref_kind {
	GREMLIND_TYPE_REF_UNRESOLVED	= 0,
	GREMLIND_TYPE_REF_BUILTIN,
	GREMLIND_TYPE_REF_MESSAGE,
	GREMLIND_TYPE_REF_ENUM,
	GREMLIND_TYPE_REF_MAP
};

struct gremlind_type_ref {
	enum gremlind_type_ref_kind	kind;
	union {
		struct {
			const char	*start;		/* into source buffer */
			size_t		 length;
		}			builtin;
		struct gremlind_message	*message;
		struct gremlind_enum	*enumeration;
		struct {
			struct gremlind_type_ref	*key;	/* arena-allocated */
			struct gremlind_type_ref	*value;
		}			map;
	} u;
};

struct gremlind_field {
	struct gremlinp_field_parse_result	parsed;
	struct gremlind_type_ref		type;		/* set by gremlind_resolve_type_refs */
};

struct gremlind_fields {
	struct gremlind_field			*items;
	size_t					count;
};

/*
 * DescriptorPool-style flat storage: file.messages and file.enums hold
 * EVERY message / enum in the file, nested or not, in DFS pre-order of
 * declaration. Hierarchy is recovered via `parent` pointers. Keeps
 * symbol-table lookups a linear scan over one array and makes per-FQN
 * indexing straightforward.
 */
struct gremlind_message {
	struct gremlinp_message_parse_result	parsed;
	struct gremlind_fields			fields;
	struct gremlind_message			*parent;	/* NULL if top-level */
	struct gremlind_scoped_name		scoped_name;	/* set by gremlind_compute_scoped_names */
};

struct gremlind_messages {
	struct gremlind_message			*items;
	size_t					count;
};

/*
 * Set of files whose types are visible during name resolution from this
 * file: the file itself + direct imports + transitive public imports.
 * Populated by gremlind_compute_visibility; NULL/0 until then.
 */
struct gremlind_visible_files {
	struct gremlind_file			**items;	/* arena-allocated */
	size_t					 count;
};

struct gremlind_file {
	struct gremlind_syntax			syntax;
	struct gremlind_edition			edition;
	struct gremlind_package			package;
	struct gremlind_imports			imports;
	struct gremlind_enums			enums;		/* flat, all nesting levels */
	struct gremlind_messages		messages;	/* flat, all nesting levels */
	struct gremlind_visible_files		visible;	/* set by gremlind_compute_visibility */
};

#endif /* !_GREMLIND_NODES_H_ */

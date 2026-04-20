#include <string.h>

#include "gremlinc/emit.h"
#include "gremlinc/naming.h"

#include "emit_common.h"

enum gremlinp_parsing_error
gremlinc_emit_enum(struct gremlinc_writer *w,
		   struct gremlinc_name_scope *scope,
		   const struct gremlind_enum *e)
{
	if (w == NULL || scope == NULL || e == NULL) {
		return GREMLINP_ERROR_NULL_POINTER;
	}

	/* c_name is populated by the gremlinc_assign_c_names pre-pass;
	 * refuse if caller skipped it (would otherwise double-register). */
	const char *tname = e->c_name;
	if (tname == NULL) return GREMLINP_ERROR_NULL_POINTER;

	enum gremlinp_parsing_error err;

	W("typedef enum ");
	W(tname);
	W(" {\n");

	for (size_t i = 0; i < e->values.count; i++) {
		const struct gremlind_enum_value *v = &e->values.items[i];

		/* Register the value's C identifier (<tname>_<VALUE_NAME>) in
		 * the same scope so future types can't collide with it. Also
		 * handles value-level C-keyword collisions (e.g. a proto enum
		 * with a value literally named `int`). */
		size_t tname_len = strlen(tname);
		size_t raw_len = tname_len + 1 + v->parsed.name_length;
		char *raw = gremlind_arena_alloc(scope->arena, raw_len + 1);
		if (raw == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		memcpy(raw, tname, tname_len);
		raw[tname_len] = '_';
		memcpy(raw + tname_len + 1, v->parsed.name_start, v->parsed.name_length);
		raw[raw_len] = '\0';

		const char *vname = gremlinc_name_scope_mangle(scope, raw, raw_len);
		if (vname == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;

		W("\t");
		W(vname);
		W(" = ");
		WI(v->parsed.index);
		W(",\n");
	}

	W("} ");
	W(tname);
	/* Trailing blank line matches style(9): one empty line between
	 * top-level definitions. Callers concatenating multiple emits get
	 * properly-separated output with no extra logic. */
	W(";\n\n");

	return GREMLINP_OK;
}

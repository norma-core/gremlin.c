#include <stdlib.h>
#include <string.h>

#include "emit_file.h"

/* An earlier message in the order has a field of message at: its contracts
 * then name at's reader before at is emitted, which mutual recursion does. */
static bool
named_before(const struct gremlind_message_order *o, size_t at)
{
	const struct gremlind_message *m = o->items[at];

	for (size_t i = 0; i < at; i++) {
		const struct gremlind_message *p = o->items[i];
		for (size_t k = 0; k < p->fields.count; k++) {
			const struct gremlind_type_ref *t = &p->fields.items[k].type;
			if (t->kind == GREMLIND_TYPE_REF_MAP) t = t->u.map.value;
			if (t != NULL && t->kind == GREMLIND_TYPE_REF_MESSAGE &&
			    t->u.message == m)
				return true;
		}
	}
	return false;
}

/* Every message into its own buffer, in the same order so the mangler
 * claims the same names; then the reader struct and predicate of each
 * message named before it is emitted go first. */
static enum gremlinp_parsing_error
emit_messages_hoisted(struct gremlinc_writer *w, struct gremlinc_name_scope *scope,
		      const struct gremlind_message_order *o)
{
	enum gremlinp_parsing_error err = GREMLINP_ERROR_OUT_OF_MEMORY;
	struct gremlinc_writer *bufs = calloc(o->count, sizeof(*bufs));
	size_t *start = calloc(o->count, sizeof(*start));
	size_t *end = calloc(o->count, sizeof(*end));
	size_t made = 0;

	if (bufs == NULL || start == NULL || end == NULL) goto out;
	for (; made < o->count; made++) {
		if (!gremlinc_writer_init_owned(&bufs[made], 16 * 1024)) {
			err = GREMLINP_ERROR_OUT_OF_MEMORY;
			goto out;
		}
		err = gremlinc_emit_message_marked(&bufs[made], scope, o->items[made],
						   &start[made], &end[made]);
		if (err != GREMLINP_OK) {
			made++;
			goto out;
		}
	}
	for (size_t i = 0; i < o->count; i++) {
		if (!named_before(o, i)) continue;
		err = gremlinc_write(w, bufs[i].buf + start[i], end[i] - start[i]);
		if (err != GREMLINP_OK) goto out;
	}
	for (size_t i = 0; i < o->count; i++) {
		const struct gremlinc_writer *b = &bufs[i];
		if (!named_before(o, i)) {
			err = gremlinc_write(w, b->buf, b->offset);
		} else {
			err = gremlinc_write(w, b->buf, start[i]);
			if (err == GREMLINP_OK)
				err = gremlinc_write(w, b->buf + end[i], b->offset - end[i]);
		}
		if (err != GREMLINP_OK) goto out;
	}
	err = GREMLINP_OK;
out:
	for (size_t i = 0; i < made; i++) gremlinc_writer_dispose(&bufs[i]);
	free(bufs);
	free(start);
	free(end);
	return err;
}

static char *
derive_guard(const char *src_path)
{
	size_t plen = strlen(src_path);
	/* "_" prefix + each char maps to one upper-case char + "_PB_H_" suffix + NUL. */
	char *g = malloc(1 + plen + 6 + 1);
	if (g == NULL) return NULL;
	size_t o = 0;
	g[o++] = '_';
	for (size_t i = 0; i < plen; i++) {
		char c = src_path[i];
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) g[o++] = c;
		else if (c >= 'a' && c <= 'z') g[o++] = (char)(c - 'a' + 'A');
		else g[o++] = '_';
	}
	memcpy(g + o, "_PB_H_", 7);
	return g;
}

enum gremlinp_parsing_error
emit_file(struct gremlind_arena *arena,
	  struct gremlinc_name_scope *global_scope,
	  const struct gremlind_file *file,
	  const char *src_path,
	  struct gremlinc_writer *w)
{
	enum gremlinp_parsing_error err;

	char *guard = derive_guard(src_path);
	if (guard == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;

	err = gremlinc_write_cstr(w, "#ifndef ");  if (err) goto out;
	err = gremlinc_write_cstr(w, guard);       if (err) goto out;
	err = gremlinc_write_cstr(w, "\n#define "); if (err) goto out;
	err = gremlinc_write_cstr(w, guard);       if (err) goto out;
	err = gremlinc_write_cstr(w, "\n\n");
	if (err) goto out;

	/* Stdlib includes are gated by codegen-hint flags that descriptors
	 * sets during `gremlind_build_file` — see `struct gremlind_file`. */
	if (file->needs_math_h) {
		err = gremlinc_write_cstr(w, "#include <math.h>\n");
		if (err) goto out;
	}
	if (file->needs_string_h) {
		err = gremlinc_write_cstr(w, "#include <string.h>\n");
		if (err) goto out;
	}
	err = gremlinc_write_cstr(w,
		"#include <stdbool.h>\n"
		"#include \"gremlin.h\"\n");
	if (err) goto out;

	/* Pull in every imported file's generated header so types it
	 * declares (referenced by our fields) are visible at compile
	 * time.  Imports map 1:1 onto generated files: `foo/bar.proto`
	 * → `foo/bar.pb.h` in the same layout as our output tree. */
	for (size_t i = 0; i < file->imports.count; i++) {
		const struct gremlinp_import_parse_result *ip =
			&file->imports.items[i].parsed;
		err = gremlinc_write_cstr(w, "#include \"");
		if (err) goto out;
		size_t plen = ip->path_length;
		/* Strip trailing ".proto" to substitute ".pb.h". */
		if (plen > 6 && memcmp(ip->path_start + plen - 6, ".proto", 6) == 0)
			plen -= 6;
		err = gremlinc_write(w, ip->path_start, plen);
		if (err) goto out;
		err = gremlinc_write_cstr(w, ".pb.h\"\n");
		if (err) goto out;
	}
	err = gremlinc_write_cstr(w, "\n");
	if (err) goto out;

	/* Enums are always leaves — emit in declaration order. */
	for (size_t i = 0; i < file->enums.count; i++) {
		err = gremlinc_emit_enum(w, global_scope, &file->enums.items[i]);
		if (err != GREMLINP_OK) goto out;
	}

	/* Messages in topo order; `predeclare` is the subset that need
	 * `typedef struct X X;` forward decls because the topo order
	 * still contains back-edges (self-recursive + mutually-recursive
	 * field types). */
	struct gremlind_message_order order;
	err = gremlind_topo_sort_messages(arena,
					  (struct gremlind_file *)file,
					  &order);
	if (err != GREMLINP_OK) goto out;

	for (size_t i = 0; i < order.predeclare_count; i++) {
		err = gremlinc_emit_message_forward(w, order.predeclare[i]);
		if (err != GREMLINP_OK) goto out;
	}
	bool hoist = false;
	for (size_t i = 0; i < order.count && !hoist; i++)
		hoist = named_before(&order, i);
	if (hoist) {
		err = emit_messages_hoisted(w, global_scope, &order);
		if (err != GREMLINP_OK) goto out;
	} else {
		for (size_t i = 0; i < order.count; i++) {
			err = gremlinc_emit_message(w, global_scope, order.items[i]);
			if (err != GREMLINP_OK) goto out;
		}
	}

	gremlinc_writer_trim_trailing_blank(w);
	err = gremlinc_write_cstr(w, "\n#endif\n");

out:
	free(guard);
	return err;
}

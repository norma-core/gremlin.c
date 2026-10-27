#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "emit_file.h"

struct msg_pos {
	const struct gremlind_message	*m;
	size_t				 at;
};

static int
cmp_msg_pos(const void *a, const void *b)
{
	uintptr_t x = (uintptr_t)((const struct msg_pos *)a)->m;
	uintptr_t y = (uintptr_t)((const struct msg_pos *)b)->m;
	return (x > y) - (x < y);
}

/* hoist[i]: a message earlier in the order holds message i, so its
 * contracts name i's reader before i is emitted. */
static enum gremlinp_parsing_error
mark_named_before(const struct gremlind_message_order *o, bool *hoist, bool *any)
{
	struct msg_pos *pos;

	*any = false;
	if (o->count == 0) return GREMLINP_OK;
	pos = malloc(o->count * sizeof(*pos));
	if (pos == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < o->count; i++) {
		pos[i].m = o->items[i];
		pos[i].at = i;
	}
	qsort(pos, o->count, sizeof(*pos), cmp_msg_pos);
	for (size_t i = 0; i < o->count; i++) {
		const struct gremlind_message *p = o->items[i];
		for (size_t k = 0; k < p->fields.count; k++) {
			const struct gremlind_type_ref *t = &p->fields.items[k].type;
			if (t->kind == GREMLIND_TYPE_REF_MAP) t = t->u.map.value;
			if (t == NULL || t->kind != GREMLIND_TYPE_REF_MESSAGE) continue;
			struct msg_pos key = { t->u.message, 0 };
			const struct msg_pos *hit = bsearch(&key, pos, o->count, sizeof(*pos),
							    cmp_msg_pos);
			if (hit != NULL && hit->at > i) {
				hoist[hit->at] = true;
				*any = true;
			}
		}
	}
	free(pos);
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
emit_file_names_ahead(struct gremlind_arena *arena, const struct gremlind_file *file,
		      bool *out)
{
	struct gremlind_message_order order;
	enum gremlinp_parsing_error err;
	bool *hoist;

	*out = false;
	err = gremlind_topo_sort_messages(arena, (struct gremlind_file *)file, &order);
	if (err != GREMLINP_OK || order.count == 0) return err;
	hoist = calloc(order.count, sizeof(*hoist));
	if (hoist == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
	err = mark_named_before(&order, hoist, out);
	free(hoist);
	return err;
}

/* The reader struct and predicate of each hoisted message go first; every
 * name a contract uses was learned by the caller's first pass. */
static enum gremlinp_parsing_error
emit_messages_hoisted(struct gremlinc_writer *w, struct gremlinc_name_scope *scope,
		      const struct gremlinc_name_scope *reserved,
		      const struct gremlind_message_order *o, const bool *hoist)
{
	enum gremlinp_parsing_error err = GREMLINP_ERROR_OUT_OF_MEMORY;
	struct gremlinc_writer tmp = { 0 };
	/* Per message: start, reader decl start, reader decl end, end. */
	size_t *at = calloc(o->count * 4u, sizeof(*at));

	if (at == NULL || !gremlinc_writer_init_owned(&tmp, 64 * 1024)) goto out;
	for (size_t i = 0; i < o->count; i++) {
		at[4 * i] = tmp.offset;
		err = gremlinc_emit_message_marked(&tmp, scope, reserved, o->items[i],
						   &at[4 * i + 1], &at[4 * i + 2]);
		if (err != GREMLINP_OK) goto out;
		at[4 * i + 3] = tmp.offset;
	}
	for (size_t i = 0; i < o->count; i++) {
		if (!hoist[i]) continue;
		err = gremlinc_write(w, tmp.buf + at[4 * i + 1], at[4 * i + 2] - at[4 * i + 1]);
		if (err != GREMLINP_OK) goto out;
	}
	for (size_t i = 0; i < o->count; i++) {
		const size_t *a = &at[4 * i];
		if (!hoist[i]) {
			err = gremlinc_write(w, tmp.buf + a[0], a[3] - a[0]);
		} else {
			err = gremlinc_write(w, tmp.buf + a[0], a[1] - a[0]);
			if (err == GREMLINP_OK)
				err = gremlinc_write(w, tmp.buf + a[2], a[3] - a[2]);
		}
		if (err != GREMLINP_OK) goto out;
	}
	err = GREMLINP_OK;
out:
	gremlinc_writer_dispose(&tmp);
	free(at);
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
	  const struct gremlinc_name_scope *reserved,
	  const struct gremlind_file *file,
	  const char *src_path,
	  struct gremlinc_writer *w)
{
	enum gremlinp_parsing_error err;

	char *guard = derive_guard(src_path);
	bool *hoist = NULL;
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
	bool any = false;
	hoist = order.count > 0 ? calloc(order.count, sizeof(*hoist)) : NULL;
	if (order.count > 0 && hoist == NULL) {
		err = GREMLINP_ERROR_OUT_OF_MEMORY;
		goto out;
	}
	err = mark_named_before(&order, hoist, &any);
	if (err != GREMLINP_OK) goto out;
	if (any) {
		err = emit_messages_hoisted(w, global_scope, reserved, &order, hoist);
		if (err != GREMLINP_OK) goto out;
	} else {
		for (size_t i = 0; i < order.count; i++) {
			err = gremlinc_emit_message(w, global_scope, reserved, order.items[i]);
			if (err != GREMLINP_OK) goto out;
		}
	}

	gremlinc_writer_trim_trailing_blank(w);
	err = gremlinc_write_cstr(w, "\n#endif\n");

out:
	free(hoist);
	free(guard);
	return err;
}

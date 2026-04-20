#include <string.h>

#include "gremlind/arena.h"
#include "gremlind/extend.h"
#include "gremlind/name.h"
#include "gremlind/nodes.h"
#include "gremlind/resolve.h"

#include "gremlinp/entries.h"

/*
 * Pipeline pass: propagate `extend T { ... }` fields onto the target
 * message T. See gremlind/extend.h for placement + invariants.
 *
 * The builder (build.c) walks the parser buffer structurally and
 * routes entries by kind, but deliberately drops EXTEND entries in
 * the switch's default arm — they can't be processed in-band because
 * resolving the target T requires scoped_names + visibility to be
 * populated. This pass re-walks the buffer, picks out just the extend
 * entries (top-level + nested), resolves T, parses the extend body's
 * fields with gremlinp_field_parse, and appends them to T's fields[].
 *
 * Memory: field arrays are arena-allocated and exact-sized by
 * build.c. Growing them here means allocating a fresh array of
 * (old_count + n_new) elements, copying the originals, and appending
 * the new ones. The old array leaks in the arena — acceptable since
 * arenas don't free, and extends are infrequent compared to the rest
 * of the descriptor data.
 */

/* Re-walk helper: open a sub-range of the parser buffer (a message
 * body), restoring the original offset + buf_size on exit. Matches
 * the pattern in build.c so the same buffer can be walked
 * structurally multiple times. */
struct scope {
	size_t	saved_offset;
	size_t	saved_buf_size;
};

static void
scope_enter(struct scope *s, struct gremlinp_parser_buffer *buf,
	    size_t body_start, size_t body_end)
{
	s->saved_offset = buf->offset;
	s->saved_buf_size = buf->buf_size;
	buf->offset = body_start;
	buf->buf_size = body_end;
}

static void
scope_leave(const struct scope *s, struct gremlinp_parser_buffer *buf)
{
	buf->offset = s->saved_offset;
	buf->buf_size = s->saved_buf_size;
}

static bool
at_eof(struct gremlinp_parser_buffer *buf)
{
	gremlinp_parser_buffer_skip_spaces(buf);
	return buf->offset >= buf->buf_size;
}

/* Find a message in F.visible whose scoped_name equals `target`.  Name
 * resolution mirrors resolve.c's lookup_in_visible — scoped-name
 * equality across every message of every visible file. */
static struct gremlind_message *
lookup_message(const struct gremlind_visible_files *visible,
	       const struct gremlind_scoped_name *target)
{
	for (size_t i = 0; i < visible->count; i++) {
		struct gremlind_file *f = visible->items[i];
		for (size_t m = 0; m < f->messages.count; m++) {
			if (gremlind_scoped_name_eq(&f->messages.items[m].scoped_name, target)) {
				return &f->messages.items[m];
			}
		}
	}
	return NULL;
}

/* Resolve an extend's base_type span to a target message, using the
 * protobuf scope walk against `enclosing` (the scoped name of the
 * message the extend is declared in, or empty for top-level extends).
 * Mirrors resolve.c::resolve_named_type but specialised to messages. */
static enum gremlinp_parsing_error
resolve_extend_target(struct gremlind_arena *arena,
		      const char *span_start, size_t span_len,
		      const struct gremlind_scoped_name *enclosing,
		      const struct gremlind_visible_files *visible,
		      struct gremlind_message **out)
{
	*out = NULL;
	if (span_start == NULL || span_len == 0) {
		return GREMLINP_ERROR_EXTEND_SOURCE_NOT_FOUND;
	}

	struct gremlind_scoped_name ref;
	enum gremlinp_parsing_error err = gremlind_scoped_name_parse(
		arena, span_start, span_len, &ref);
	if (err != GREMLINP_OK) return err;

	if (ref.absolute) {
		struct gremlind_message *m = lookup_message(visible, &ref);
		if (m == NULL) return GREMLINP_ERROR_EXTEND_SOURCE_NOT_FOUND;
		*out = m;
		return GREMLINP_OK;
	}

	size_t n_segs = (enclosing != NULL) ? enclosing->n_segments : 0;
	for (size_t k = n_segs + 1; k-- > 0; ) {
		struct gremlind_scoped_name prefix = {
			.segments   = (enclosing != NULL) ? enclosing->segments : NULL,
			.n_segments = k,
			.absolute   = true
		};
		struct gremlind_scoped_name candidate;
		err = gremlind_scoped_name_extend(arena, &prefix,
			ref.segments, ref.n_segments, &candidate);
		if (err != GREMLINP_OK) return err;

		struct gremlind_message *m = lookup_message(visible, &candidate);
		if (m != NULL) { *out = m; return GREMLINP_OK; }
	}

	return GREMLINP_ERROR_EXTEND_SOURCE_NOT_FOUND;
}

/* Walk an extend body, parsing each field with gremlinp_field_parse.
 * If `fields_out` is non-NULL, append to it at `*fi_out` (pre-counted).
 * Otherwise just count. */
static enum gremlinp_parsing_error
walk_extend_body(struct gremlinp_parser_buffer *buf,
		 size_t body_start, size_t body_end,
		 struct gremlind_field *fields_out,
		 size_t *fi_out, size_t *count_out)
{
	struct scope s;
	scope_enter(&s, buf, body_start, body_end);

	while (buf->offset < buf->buf_size) {
		enum gremlinp_parsing_error err = gremlinp_parser_buffer_skip_spaces(buf);
		if (err != GREMLINP_OK) { scope_leave(&s, buf); return err; }
		if (buf->offset >= buf->buf_size) break;
		char c = gremlinp_parser_buffer_char(buf);
		if (c == ';') { buf->offset++; continue; }

		struct gremlinp_field_parse_result fr = gremlinp_field_parse(buf);
		if (fr.error != GREMLINP_OK) { scope_leave(&s, buf); return fr.error; }

		if (fields_out != NULL) {
			struct gremlind_field *fd = &fields_out[(*fi_out)++];
			memset(fd, 0, sizeof *fd);
			fd->parsed = fr;
		}
		if (count_out != NULL) (*count_out)++;
	}

	scope_leave(&s, buf);
	return GREMLINP_OK;
}

/* Process one extend entry: resolve target + count, or resolve
 * target + grow target->fields with the body's fields. Separates the
 * two modes to keep the growth allocation exact-sized. */
static enum gremlinp_parsing_error
apply_extend(struct gremlind_arena *arena,
	     struct gremlind_file *origin_file,
	     struct gremlinp_parser_buffer *buf,
	     const struct gremlinp_extend_parse_result *ext,
	     const struct gremlind_scoped_name *enclosing,
	     const struct gremlind_scoped_name *persistent_scope)
{
	struct gremlind_message *target;
	enum gremlinp_parsing_error err = resolve_extend_target(arena,
		ext->base_type.start, ext->base_type.length,
		enclosing, &origin_file->visible, &target);
	if (err != GREMLINP_OK) return err;

	size_t n_new = 0;
	err = walk_extend_body(buf, ext->body_start, ext->body_end,
		NULL, NULL, &n_new);
	if (err != GREMLINP_OK) return err;
	if (n_new == 0) return GREMLINP_OK;

	size_t old_count = target->fields.count;
	size_t new_count = old_count + n_new;
	struct gremlind_field *merged = gremlind_arena_alloc(arena,
		new_count * sizeof(*merged));
	if (merged == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;

	if (old_count > 0) {
		memcpy(merged, target->fields.items,
		       old_count * sizeof(*merged));
	}

	size_t fi = old_count;
	err = walk_extend_body(buf, ext->body_start, ext->body_end,
		merged, &fi, NULL);
	if (err != GREMLINP_OK) return err;

	/* Tag the appended fields with their origin file + declaration
	 * scope so type-ref resolution walks the extending file's
	 * visibility + the declaration's scope, not the target's.
	 * Synthesized extension fields do not carry `[default]` —
	 * build.c's extract_default_option would have to be shared
	 * here and proto2 extensions with defaults are out of current
	 * v1 scope. */
	for (size_t i = old_count; i < new_count; i++) {
		merged[i].origin_file = origin_file;
		merged[i].origin_scope = persistent_scope;
	}

	target->fields.items = merged;
	target->fields.count = new_count;
	return GREMLINP_OK;
}

/* Walk the entries of a message body (possibly recursive), dispatching
 * on nested MESSAGE (recurse) and EXTEND (apply). Other kinds pass
 * through; build.c already processed them. */
static enum gremlinp_parsing_error
walk_message_body(struct gremlind_arena *arena,
		  struct gremlind_file *origin_file,
		  struct gremlinp_parser_buffer *buf,
		  size_t body_start, size_t body_end,
		  const struct gremlind_scoped_name *enclosing,
		  size_t *nested_msg_idx)
{
	struct scope s;
	scope_enter(&s, buf, body_start, body_end);

	while (1) {
		struct gremlinp_message_entry_result e =
			gremlinp_message_next_entry(buf);
		if (e.error != GREMLINP_OK) {
			if (at_eof(buf)) { scope_leave(&s, buf); return GREMLINP_OK; }
			scope_leave(&s, buf);
			return e.error;
		}
		if (e.kind == GREMLINP_MSG_ENTRY_MESSAGE) {
			/* Advance the nested-message cursor: messages are laid
			 * out in the file's flat array in DFS pre-order by
			 * build.c, so the next slot corresponds to this nested
			 * message. Its scoped_name is what inner extends
			 * resolve against. */
			size_t idx = (*nested_msg_idx)++;
			const struct gremlind_scoped_name *inner_scope =
				&origin_file->messages.items[idx].scoped_name;
			enum gremlinp_parsing_error err = walk_message_body(
				arena, origin_file, buf,
				e.u.message.body_start, e.u.message.body_end,
				inner_scope, nested_msg_idx);
			if (err != GREMLINP_OK) { scope_leave(&s, buf); return err; }
		} else if (e.kind == GREMLINP_MSG_ENTRY_EXTEND) {
			enum gremlinp_parsing_error err = apply_extend(
				arena, origin_file, buf, &e.u.extend,
				enclosing, enclosing);
			if (err != GREMLINP_OK) { scope_leave(&s, buf); return err; }
		}
	}
}

/* Build a scoped_name from the file's `package foo.bar.baz;` declaration.
 * Top-level extends use this as their enclosing scope so the scope walk
 * tries fully-qualified names before bare names. If no package is
 * declared, returns an empty scoped name (walk collapses to one step). */
static enum gremlinp_parsing_error
make_package_scope(struct gremlind_arena *arena,
		   const struct gremlind_file *f,
		   struct gremlind_scoped_name *out)
{
	out->segments = NULL;
	out->n_segments = 0;
	out->absolute = true;
	if (!f->package.present) return GREMLINP_OK;
	return gremlind_scoped_name_parse(arena,
		f->package.value.name_start,
		f->package.value.name_length, out);
}

static enum gremlinp_parsing_error
walk_file(struct gremlind_arena *arena,
	  struct gremlind_file *origin_file,
	  struct gremlinp_parser_buffer *buf)
{
	size_t saved_offset = buf->offset;
	buf->offset = 0;

	/* pkg_scope needs to outlive this function — synthesized extension
	 * fields hold a pointer to it for later resolution — so allocate
	 * the struct itself in the arena (its segments array already is). */
	struct gremlind_scoped_name *pkg_scope = gremlind_arena_alloc(
		arena, sizeof *pkg_scope);
	if (pkg_scope == NULL) { buf->offset = saved_offset; return GREMLINP_ERROR_OUT_OF_MEMORY; }
	enum gremlinp_parsing_error result = make_package_scope(arena,
		origin_file, pkg_scope);
	if (result != GREMLINP_OK) { buf->offset = saved_offset; return result; }

	size_t nested_msg_idx = 0;

	while (1) {
		struct gremlinp_file_entry_result e = gremlinp_file_next_entry(buf);
		if (e.error != GREMLINP_OK) {
			if (at_eof(buf)) break;
			result = e.error;
			break;
		}
		if (e.kind == GREMLINP_FILE_ENTRY_MESSAGE) {
			size_t idx = nested_msg_idx++;
			const struct gremlind_scoped_name *scope =
				&origin_file->messages.items[idx].scoped_name;
			result = walk_message_body(arena, origin_file, buf,
				e.u.message.body_start, e.u.message.body_end,
				scope, &nested_msg_idx);
			if (result != GREMLINP_OK) break;
		} else if (e.kind == GREMLINP_FILE_ENTRY_EXTEND) {
			result = apply_extend(arena, origin_file, buf,
				&e.u.extend, pkg_scope, pkg_scope);
			if (result != GREMLINP_OK) break;
		}
	}

	buf->offset = saved_offset;
	return result;
}

enum gremlinp_parsing_error
gremlind_propagate_extends(struct gremlind_resolve_context *ctx)
{
	if (ctx == NULL || (ctx->files == NULL && ctx->n_sources > 0)) {
		if (ctx) ctx->error = GREMLINP_ERROR_NULL_POINTER;
		return GREMLINP_ERROR_NULL_POINTER;
	}

	for (size_t i = 0; i < ctx->n_sources; i++) {
		struct gremlind_file *f = ctx->files[i];
		if (f == NULL) continue;
		enum gremlinp_parsing_error err = walk_file(
			ctx->arena, f, &ctx->sources[i].buf);
		if (err != GREMLINP_OK) {
			ctx->error = err;
			ctx->failed_source_idx = i;
			return err;
		}
	}

	return GREMLINP_OK;
}

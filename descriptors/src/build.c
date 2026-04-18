#include <string.h>

#include "gremlind/arena.h"
#include "gremlind/build.h"
#include "gremlind/nodes.h"
#include "gremlinp/lib.h"

/*
 * Two-pass builder. Pass 1 counts totals across the whole (possibly
 * nested) proto; pass 2 allocates exact-size flat arrays on the file
 * and fills them in DFS pre-order.
 *
 * Nested bodies are iterated on the SAME parser buffer — offset and
 * buf_size are saved, clamped to [body_start, body_end), and restored
 * after. Avoids recomputing strlen over the full source for every sub-
 * body.
 */

struct counts {
	size_t n_imports;
	size_t n_enums;			/* all nesting levels */
	size_t n_messages;		/* all nesting levels */
};

/* Running indices into the file's flat arrays during pass 2. */
struct fill_state {
	struct gremlind_file	*file;
	size_t			 msg_idx;
	size_t			 enum_idx;
};

/*
 * On any error, gremlinp_*_next_entry restores buf->offset to where it
 * was before the call (speculative retry). That means after a successful
 * last entry with trailing whitespace, the next call lands
 * UNEXPECTED_TOKEN with offset *before* the whitespace. To detect a
 * genuine end-of-buffer we skip whitespace ourselves and compare to
 * buf_size.
 */
static bool
at_eof(struct gremlinp_parser_buffer *buf)
{
	gremlinp_parser_buffer_skip_spaces(buf);
	return buf->offset >= buf->buf_size;
}

struct body_scope {
	size_t saved_offset;
	size_t saved_buf_size;
};

static void
body_scope_enter(struct body_scope *s, struct gremlinp_parser_buffer *buf,
		 size_t body_start, size_t body_end)
{
	s->saved_offset = buf->offset;
	s->saved_buf_size = buf->buf_size;
	buf->offset = body_start;
	buf->buf_size = body_end;
}

static void
body_scope_leave(struct body_scope *s, struct gremlinp_parser_buffer *buf)
{
	buf->offset = s->saved_offset;
	buf->buf_size = s->saved_buf_size;
}

/* Forward decls — mutual recursion for counting and filling. */
static enum gremlinp_parsing_error count_message_body(struct gremlinp_parser_buffer *buf,
	size_t body_start, size_t body_end, struct counts *out);
static enum gremlinp_parsing_error fill_message(struct gremlind_arena *arena,
	struct gremlinp_parser_buffer *buf, struct fill_state *state,
	const struct gremlinp_message_parse_result *parsed,
	struct gremlind_message *parent);

static enum gremlinp_parsing_error
count_entries(struct gremlinp_parser_buffer *buf, struct counts *out)
{
	memset(out, 0, sizeof(*out));

	for (;;) {
		struct gremlinp_file_entry_result e = gremlinp_file_next_entry(buf);
		if (e.error != GREMLINP_OK) {
			if (at_eof(buf)) return GREMLINP_OK;
			return e.error;
		}
		switch (e.kind) {
		case GREMLINP_FILE_ENTRY_IMPORT:
			out->n_imports++;
			break;
		case GREMLINP_FILE_ENTRY_ENUM:
			out->n_enums++;
			break;
		case GREMLINP_FILE_ENTRY_MESSAGE:
			out->n_messages++;
			/* Walk into the message body so nested enums / messages
			 * are counted against the flat file totals. */
			{
				enum gremlinp_parsing_error err = count_message_body(buf,
					e.u.message.body_start, e.u.message.body_end, out);
				if (err != GREMLINP_OK) return err;
			}
			break;
		default:
			break;
		}
	}
}

static enum gremlinp_parsing_error
count_message_body(struct gremlinp_parser_buffer *buf,
		   size_t body_start, size_t body_end, struct counts *out)
{
	struct body_scope scope;
	body_scope_enter(&scope, buf, body_start, body_end);

	for (;;) {
		struct gremlinp_message_entry_result e = gremlinp_message_next_entry(buf);
		if (e.error != GREMLINP_OK) {
			if (at_eof(buf)) { body_scope_leave(&scope, buf); return GREMLINP_OK; }
			body_scope_leave(&scope, buf);
			return e.error;
		}
		switch (e.kind) {
		case GREMLINP_MSG_ENTRY_ENUM:
			out->n_enums++;
			break;
		case GREMLINP_MSG_ENTRY_MESSAGE:
			out->n_messages++;
			{
				enum gremlinp_parsing_error err = count_message_body(buf,
					e.u.message.body_start, e.u.message.body_end, out);
				if (err != GREMLINP_OK) {
					body_scope_leave(&scope, buf);
					return err;
				}
			}
			break;
		default:
			break;
		}
	}
}

/*
 * Fill an enum into the flat file.enums[enum_idx] slot, including its
 * values array. parent is the enclosing message, or NULL at top level.
 */
static enum gremlinp_parsing_error
fill_enum(struct gremlind_arena *arena, struct gremlinp_parser_buffer *buf,
	  struct fill_state *state,
	  const struct gremlinp_enum_parse_result *parsed,
	  struct gremlind_message *parent)
{
	struct gremlind_enum *out = &state->file->enums.items[state->enum_idx++];
	out->parsed = *parsed;
	out->parent = parent;
	out->values.items = NULL;
	out->values.count = 0;

	/* Count values in this enum's body. */
	size_t n_values = 0;
	{
		struct body_scope scope;
		body_scope_enter(&scope, buf, parsed->body_start, parsed->body_end);
		for (;;) {
			struct gremlinp_enum_entry_result e = gremlinp_enum_next_entry(buf);
			if (e.error != GREMLINP_OK) {
				if (at_eof(buf)) break;
				body_scope_leave(&scope, buf);
				return e.error;
			}
			if (e.kind == GREMLINP_ENUM_ENTRY_FIELD) n_values++;
		}
		body_scope_leave(&scope, buf);
	}

	if (n_values == 0) return GREMLINP_OK;

	out->values.items = gremlind_arena_alloc(arena,
		n_values * sizeof(*out->values.items));
	if (out->values.items == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
	out->values.count = n_values;

	struct body_scope scope;
	body_scope_enter(&scope, buf, parsed->body_start, parsed->body_end);
	size_t idx = 0;
	for (;;) {
		struct gremlinp_enum_entry_result e = gremlinp_enum_next_entry(buf);
		if (e.error != GREMLINP_OK) {
			if (at_eof(buf)) break;
			body_scope_leave(&scope, buf);
			return e.error;
		}
		if (e.kind == GREMLINP_ENUM_ENTRY_FIELD) {
			out->values.items[idx++].parsed = e.u.field;
		}
	}
	body_scope_leave(&scope, buf);

	return GREMLINP_OK;
}

static enum gremlinp_parsing_error
fill_message(struct gremlind_arena *arena, struct gremlinp_parser_buffer *buf,
	     struct fill_state *state,
	     const struct gremlinp_message_parse_result *parsed,
	     struct gremlind_message *parent)
{
	struct gremlind_message *out = &state->file->messages.items[state->msg_idx++];
	out->parsed = *parsed;
	out->parent = parent;
	out->fields.items = NULL;
	out->fields.count = 0;

	/* Local count of this message's own fields (not recursive). */
	size_t n_fields = 0;
	{
		struct body_scope scope;
		body_scope_enter(&scope, buf, parsed->body_start, parsed->body_end);
		for (;;) {
			struct gremlinp_message_entry_result e = gremlinp_message_next_entry(buf);
			if (e.error != GREMLINP_OK) {
				if (at_eof(buf)) break;
				body_scope_leave(&scope, buf);
				return e.error;
			}
			if (e.kind == GREMLINP_MSG_ENTRY_FIELD) n_fields++;
		}
		body_scope_leave(&scope, buf);
	}

	if (n_fields > 0) {
		out->fields.items = gremlind_arena_alloc(arena,
			n_fields * sizeof(*out->fields.items));
		if (out->fields.items == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
	}
	out->fields.count = n_fields;

	/* Pass 2 over this body: fields land in out->fields, nested enums /
	 * messages get emitted into the file's flat arrays via fill_enum /
	 * fill_message (recursive), with `out` as their parent. */
	struct body_scope scope;
	body_scope_enter(&scope, buf, parsed->body_start, parsed->body_end);
	size_t fi = 0;

	for (;;) {
		struct gremlinp_message_entry_result e = gremlinp_message_next_entry(buf);
		if (e.error != GREMLINP_OK) {
			if (at_eof(buf)) { body_scope_leave(&scope, buf); return GREMLINP_OK; }
			body_scope_leave(&scope, buf);
			return e.error;
		}
		switch (e.kind) {
		case GREMLINP_MSG_ENTRY_FIELD:
			out->fields.items[fi++].parsed = e.u.field;
			break;
		case GREMLINP_MSG_ENTRY_ENUM: {
			enum gremlinp_parsing_error r = fill_enum(arena, buf, state,
				&e.u.enumeration, out);
			if (r != GREMLINP_OK) { body_scope_leave(&scope, buf); return r; }
			break;
		}
		case GREMLINP_MSG_ENTRY_MESSAGE: {
			enum gremlinp_parsing_error r = fill_message(arena, buf, state,
				&e.u.message, out);
			if (r != GREMLINP_OK) { body_scope_leave(&scope, buf); return r; }
			break;
		}
		default:
			/* oneof / option / extensions / reserved / extend / group
			 * skipped in v1. */
			break;
		}
	}
}

static enum gremlinp_parsing_error
fill_file(struct gremlind_arena *arena, struct gremlinp_parser_buffer *buf,
	  struct gremlind_file *file)
{
	struct fill_state state = { .file = file, .msg_idx = 0, .enum_idx = 0 };
	size_t import_idx = 0;

	for (;;) {
		struct gremlinp_file_entry_result e = gremlinp_file_next_entry(buf);
		if (e.error != GREMLINP_OK) {
			if (at_eof(buf)) return GREMLINP_OK;
			return e.error;
		}

		switch (e.kind) {
		case GREMLINP_FILE_ENTRY_SYNTAX:
			file->syntax.present = true;
			file->syntax.value = e.u.syntax;
			break;
		case GREMLINP_FILE_ENTRY_EDITION:
			file->edition.present = true;
			file->edition.value = e.u.edition;
			break;
		case GREMLINP_FILE_ENTRY_PACKAGE:
			file->package.present = true;
			file->package.value = e.u.package;
			break;
		case GREMLINP_FILE_ENTRY_IMPORT:
			file->imports.items[import_idx].parsed = e.u.import;
			file->imports.items[import_idx].resolved = NULL;
			import_idx++;
			break;
		case GREMLINP_FILE_ENTRY_ENUM: {
			enum gremlinp_parsing_error err = fill_enum(arena, buf,
				&state, &e.u.enumeration, NULL);
			if (err != GREMLINP_OK) return err;
			break;
		}
		case GREMLINP_FILE_ENTRY_MESSAGE: {
			enum gremlinp_parsing_error err = fill_message(arena, buf,
				&state, &e.u.message, NULL);
			if (err != GREMLINP_OK) return err;
			break;
		}
		default:
			break;
		}
	}
}

struct gremlind_build_result
gremlind_build_file(struct gremlind_arena *arena, struct gremlinp_parser_buffer *buf)
{
	struct gremlind_build_result r;
	r.file = NULL;
	r.error = GREMLINP_OK;
	r.error_offset = 0;

	if (arena == NULL || buf == NULL) {
		r.error = GREMLINP_ERROR_NULL_POINTER;
		return r;
	}

	size_t start_offset = buf->offset;

	struct counts cnt;
	enum gremlinp_parsing_error err = count_entries(buf, &cnt);
	if (err != GREMLINP_OK) {
		r.error = err;
		r.error_offset = buf->offset;
		return r;
	}

	buf->offset = start_offset;

	struct gremlind_file *file = gremlind_arena_alloc(arena, sizeof(*file));
	if (file == NULL) {
		r.error = GREMLINP_ERROR_OUT_OF_MEMORY;
		return r;
	}
	memset(file, 0, sizeof(*file));

	if (cnt.n_imports > 0) {
		file->imports.items = gremlind_arena_alloc(arena,
			cnt.n_imports * sizeof(struct gremlind_import));
		if (file->imports.items == NULL) {
			r.error = GREMLINP_ERROR_OUT_OF_MEMORY;
			return r;
		}
	}
	file->imports.count = cnt.n_imports;

	if (cnt.n_enums > 0) {
		file->enums.items = gremlind_arena_alloc(arena,
			cnt.n_enums * sizeof(struct gremlind_enum));
		if (file->enums.items == NULL) {
			r.error = GREMLINP_ERROR_OUT_OF_MEMORY;
			return r;
		}
	}
	file->enums.count = cnt.n_enums;

	if (cnt.n_messages > 0) {
		file->messages.items = gremlind_arena_alloc(arena,
			cnt.n_messages * sizeof(struct gremlind_message));
		if (file->messages.items == NULL) {
			r.error = GREMLINP_ERROR_OUT_OF_MEMORY;
			return r;
		}
	}
	file->messages.count = cnt.n_messages;

	err = fill_file(arena, buf, file);
	if (err != GREMLINP_OK) {
		r.error = err;
		r.error_offset = buf->offset;
		return r;
	}

	r.file = file;
	return r;
}

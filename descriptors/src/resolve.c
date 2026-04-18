#include <string.h>

#include "gremlind/arena.h"
#include "gremlind/build.h"
#include "gremlind/name.h"
#include "gremlind/nodes.h"
#include "gremlind/resolve.h"

void
gremlind_resolve_context_init(struct gremlind_resolve_context *ctx,
			      struct gremlind_arena *arena,
			      struct gremlind_source *sources,
			      size_t n_sources)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->arena = arena;
	ctx->sources = sources;
	ctx->n_sources = n_sources;
}

enum gremlinp_parsing_error
gremlind_build_all(struct gremlind_resolve_context *ctx)
{
	if (ctx == NULL || ctx->arena == NULL ||
	    (ctx->sources == NULL && ctx->n_sources > 0)) {
		if (ctx) {
			ctx->error = GREMLINP_ERROR_NULL_POINTER;
		}
		return GREMLINP_ERROR_NULL_POINTER;
	}

	if (ctx->n_sources > 0) {
		ctx->files = gremlind_arena_alloc(ctx->arena,
			ctx->n_sources * sizeof(*ctx->files));
		if (ctx->files == NULL) {
			ctx->error = GREMLINP_ERROR_OUT_OF_MEMORY;
			return GREMLINP_ERROR_OUT_OF_MEMORY;
		}
	}

	for (size_t i = 0; i < ctx->n_sources; i++) {
		struct gremlind_build_result r =
			gremlind_build_file(ctx->arena, &ctx->sources[i].buf);
		if (r.error != GREMLINP_OK) {
			ctx->error = r.error;
			ctx->failed_source_idx = i;
			ctx->error_offset = r.error_offset;
			return r.error;
		}
		ctx->files[i] = r.file;
	}

	return GREMLINP_OK;
}

/*
 * Linear scan by span equality. File sets are small in practice; a
 * sorted index + binary search is a later optimisation when we prove
 * the current scan is the bottleneck on a mono-repo corpus.
 */
static struct gremlind_file *
lookup(const struct gremlind_resolve_context *ctx,
       const char *path, size_t path_len)
{
	for (size_t i = 0; i < ctx->n_sources; i++) {
		if (ctx->sources[i].path_len != path_len) {
			continue;
		}
		if (path_len == 0) {
			return ctx->files[i];
		}
		if (memcmp(ctx->sources[i].path, path, path_len) == 0) {
			return ctx->files[i];
		}
	}
	return NULL;
}

static size_t
file_index(const struct gremlind_resolve_context *ctx,
	   const struct gremlind_file *f)
{
	for (size_t i = 0; i < ctx->n_sources; i++) {
		if (ctx->files[i] == f) {
			return i;
		}
	}
	return (size_t)-1;
}

#define GREMLIND_COLOR_WHITE 0
#define GREMLIND_COLOR_GRAY  1
#define GREMLIND_COLOR_BLACK 2

struct gremlind_dfs_frame {
	size_t file_idx;
	size_t next_import;
};

enum gremlinp_parsing_error
gremlind_check_no_cycles(struct gremlind_resolve_context *ctx)
{
	if (ctx == NULL || (ctx->files == NULL && ctx->n_sources > 0)) {
		if (ctx) {
			ctx->error = GREMLINP_ERROR_NULL_POINTER;
		}
		return GREMLINP_ERROR_NULL_POINTER;
	}

	if (ctx->n_sources == 0) {
		return GREMLINP_OK;
	}

	unsigned char *color = gremlind_arena_alloc(ctx->arena, ctx->n_sources);
	struct gremlind_dfs_frame *stack = gremlind_arena_alloc(ctx->arena,
		ctx->n_sources * sizeof(*stack));
	if (color == NULL || stack == NULL) {
		ctx->error = GREMLINP_ERROR_OUT_OF_MEMORY;
		return GREMLINP_ERROR_OUT_OF_MEMORY;
	}
	for (size_t i = 0; i < ctx->n_sources; i++) {
		color[i] = GREMLIND_COLOR_WHITE;
	}

	for (size_t root = 0; root < ctx->n_sources; root++) {
		if (color[root] != GREMLIND_COLOR_WHITE) {
			continue;
		}

		size_t top = 0;
		stack[top].file_idx = root;
		stack[top].next_import = 0;
		color[root] = GREMLIND_COLOR_GRAY;

		while (1) {
			struct gremlind_dfs_frame *frame = &stack[top];
			struct gremlind_file *f = ctx->files[frame->file_idx];

			if (f == NULL || frame->next_import >= f->imports.count) {
				color[frame->file_idx] = GREMLIND_COLOR_BLACK;
				if (top == 0) {
					break;
				}
				top--;
				continue;
			}

			struct gremlind_import *imp = &f->imports.items[frame->next_import];
			frame->next_import++;

			if (imp->resolved == NULL) {
				continue;
			}
			size_t child = file_index(ctx, imp->resolved);
			if (child == (size_t)-1) {
				continue;
			}

			if (color[child] == GREMLIND_COLOR_GRAY) {
				ctx->error = GREMLINP_ERROR_CIRCULAR_IMPORT;
				ctx->failed_source_idx = child;
				return GREMLINP_ERROR_CIRCULAR_IMPORT;
			}
			if (color[child] == GREMLIND_COLOR_BLACK) {
				continue;
			}

			top++;
			stack[top].file_idx = child;
			stack[top].next_import = 0;
			color[child] = GREMLIND_COLOR_GRAY;
		}
	}

	return GREMLINP_OK;
}

static bool
contains_file(struct gremlind_file **arr, size_t n, struct gremlind_file *f)
{
	for (size_t i = 0; i < n; i++) {
		if (arr[i] == f) return true;
	}
	return false;
}

enum gremlinp_parsing_error
gremlind_compute_visibility(struct gremlind_resolve_context *ctx)
{
	if (ctx == NULL || (ctx->files == NULL && ctx->n_sources > 0)) {
		if (ctx) ctx->error = GREMLINP_ERROR_NULL_POINTER;
		return GREMLINP_ERROR_NULL_POINTER;
	}

	for (size_t i = 0; i < ctx->n_sources; i++) {
		struct gremlind_file *f = ctx->files[i];
		if (f == NULL) continue;

		/* Worst-case size of visible = n_sources. Over-allocate, then
		 * compact into a fresh arena array at the end. */
		struct gremlind_file **scratch = gremlind_arena_alloc(ctx->arena,
			ctx->n_sources * sizeof(*scratch));
		if (scratch == NULL) {
			ctx->error = GREMLINP_ERROR_OUT_OF_MEMORY;
			return GREMLINP_ERROR_OUT_OF_MEMORY;
		}
		size_t n = 0;

		scratch[n++] = f;

		/* Step 1: add every directly-imported file, regardless of its
		 * import type (public or regular). */
		for (size_t k = 0; k < f->imports.count; k++) {
			struct gremlind_file *t = f->imports.items[k].resolved;
			if (t == NULL) continue;
			if (!contains_file(scratch, n, t)) {
				scratch[n++] = t;
			}
		}

		/* Step 2: BFS from every entry after f, following ONLY public
		 * imports. f itself (index 0) is skipped so we don't follow its
		 * own public imports as transitive — they were already included
		 * as direct imports in step 1. */
		size_t processing = 1;
		while (processing < n) {
			struct gremlind_file *cur = scratch[processing++];
			for (size_t k = 0; k < cur->imports.count; k++) {
				struct gremlind_import *imp = &cur->imports.items[k];
				if (imp->parsed.type != GREMLINP_IMPORT_TYPE_PUBLIC) continue;
				struct gremlind_file *t = imp->resolved;
				if (t == NULL) continue;
				if (!contains_file(scratch, n, t)) {
					scratch[n++] = t;
				}
			}
		}

		/* Compact into exact-size arena array. */
		f->visible.items = gremlind_arena_alloc(ctx->arena,
			n * sizeof(*f->visible.items));
		if (f->visible.items == NULL) {
			ctx->error = GREMLINP_ERROR_OUT_OF_MEMORY;
			return GREMLINP_ERROR_OUT_OF_MEMORY;
		}
		memcpy(f->visible.items, scratch, n * sizeof(*scratch));
		f->visible.count = n;
	}

	return GREMLINP_OK;
}

/* ------------------------------------------------------------------------
 * Type-reference resolution
 * ------------------------------------------------------------------------ */

static const struct { const char *name; size_t length; } BUILTIN_TYPES[] = {
	{"double",   6}, {"float",    5},
	{"int32",    5}, {"int64",    5},
	{"uint32",   6}, {"uint64",   6},
	{"sint32",   6}, {"sint64",   6},
	{"fixed32",  7}, {"fixed64",  7},
	{"sfixed32", 8}, {"sfixed64", 8},
	{"bool",     4}, {"string",   6}, {"bytes", 5}
};
#define N_BUILTIN_TYPES (sizeof(BUILTIN_TYPES) / sizeof(BUILTIN_TYPES[0]))

static bool
is_builtin_span(const char *s, size_t n)
{
	for (size_t i = 0; i < N_BUILTIN_TYPES; i++) {
		if (BUILTIN_TYPES[i].length == n &&
		    memcmp(BUILTIN_TYPES[i].name, s, n) == 0) {
			return true;
		}
	}
	return false;
}

static bool
lookup_in_visible(const struct gremlind_scoped_name *target,
		  const struct gremlind_visible_files *visible,
		  struct gremlind_type_ref *out)
{
	for (size_t i = 0; i < visible->count; i++) {
		struct gremlind_file *f = visible->items[i];
		for (size_t m = 0; m < f->messages.count; m++) {
			if (gremlind_scoped_name_eq(&f->messages.items[m].scoped_name, target)) {
				out->kind = GREMLIND_TYPE_REF_MESSAGE;
				out->u.message = &f->messages.items[m];
				return true;
			}
		}
		for (size_t e = 0; e < f->enums.count; e++) {
			if (gremlind_scoped_name_eq(&f->enums.items[e].scoped_name, target)) {
				out->kind = GREMLIND_TYPE_REF_ENUM;
				out->u.enumeration = &f->enums.items[e];
				return true;
			}
		}
	}
	return false;
}

/*
 * Resolve a single type-name span (NAMED field type, or map key / value
 * spans). Recognises builtin scalars up front. Otherwise parses the span
 * into a scoped_name and does the protobuf scope walk: if absolute,
 * direct lookup; if relative, peel segments from `scope` (deepest first)
 * and try `scope[0..k] ++ ref` each time.
 *
 * On miss, returns GREMLINP_ERROR_TYPE_NOT_FOUND — caller is responsible
 * for halting the pipeline with the right context attached.
 */
static enum gremlinp_parsing_error
resolve_named_type(struct gremlind_arena *arena,
		   const char *span_start, size_t span_len,
		   const struct gremlind_scoped_name *scope,
		   const struct gremlind_visible_files *visible,
		   struct gremlind_type_ref *out)
{
	if (span_len == 0 || span_start == NULL) {
		return GREMLINP_ERROR_TYPE_NOT_FOUND;
	}

	if (is_builtin_span(span_start, span_len)) {
		out->kind = GREMLIND_TYPE_REF_BUILTIN;
		out->u.builtin.start = span_start;
		out->u.builtin.length = span_len;
		return GREMLINP_OK;
	}

	struct gremlind_scoped_name ref;
	enum gremlinp_parsing_error err = gremlind_scoped_name_parse(
		arena, span_start, span_len, &ref);
	if (err != GREMLINP_OK) return err;

	if (ref.absolute) {
		if (lookup_in_visible(&ref, visible, out)) return GREMLINP_OK;
		return GREMLINP_ERROR_TYPE_NOT_FOUND;
	}

	/* Relative: walk from deepest prefix down to empty. */
	for (size_t k = scope->n_segments + 1; k-- > 0; ) {
		struct gremlind_scoped_name prefix = {
			.segments   = scope->segments,
			.n_segments = k,
			.absolute   = true
		};
		struct gremlind_scoped_name candidate;
		err = gremlind_scoped_name_extend(arena, &prefix,
			ref.segments, ref.n_segments, &candidate);
		if (err != GREMLINP_OK) return err;

		if (lookup_in_visible(&candidate, visible, out)) return GREMLINP_OK;
	}

	return GREMLINP_ERROR_TYPE_NOT_FOUND;
}

static enum gremlinp_parsing_error
resolve_field_type(struct gremlind_arena *arena,
		   struct gremlind_field *field,
		   const struct gremlind_scoped_name *scope,
		   const struct gremlind_visible_files *visible)
{
	const struct gremlinp_field_type_parse_result *pt = &field->parsed.type;

	if (pt->kind == GREMLINP_FIELD_TYPE_NAMED) {
		return resolve_named_type(arena,
			pt->u.named.start, pt->u.named.length,
			scope, visible, &field->type);
	}

	/* MAP — allocate key + value type_refs and resolve each. Key must
	 * be a builtin scalar per the grammar; the parser validates that,
	 * so here resolve_named_type will just return a BUILTIN ref. */
	struct gremlind_type_ref *kr = gremlind_arena_alloc(arena, sizeof(*kr));
	struct gremlind_type_ref *vr = gremlind_arena_alloc(arena, sizeof(*vr));
	if (kr == NULL || vr == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;

	enum gremlinp_parsing_error err;
	err = resolve_named_type(arena,
		pt->u.map.key_type.start, pt->u.map.key_type.length,
		scope, visible, kr);
	if (err != GREMLINP_OK) return err;
	err = resolve_named_type(arena,
		pt->u.map.value_type.start, pt->u.map.value_type.length,
		scope, visible, vr);
	if (err != GREMLINP_OK) return err;

	field->type.kind = GREMLIND_TYPE_REF_MAP;
	field->type.u.map.key = kr;
	field->type.u.map.value = vr;
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlind_resolve_type_refs(struct gremlind_resolve_context *ctx)
{
	if (ctx == NULL || (ctx->files == NULL && ctx->n_sources > 0)) {
		if (ctx) ctx->error = GREMLINP_ERROR_NULL_POINTER;
		return GREMLINP_ERROR_NULL_POINTER;
	}

	for (size_t i = 0; i < ctx->n_sources; i++) {
		struct gremlind_file *f = ctx->files[i];
		if (f == NULL) continue;

		for (size_t m = 0; m < f->messages.count; m++) {
			struct gremlind_message *msg = &f->messages.items[m];
			for (size_t fi = 0; fi < msg->fields.count; fi++) {
				struct gremlind_field *fd = &msg->fields.items[fi];
				enum gremlinp_parsing_error err = resolve_field_type(
					ctx->arena, fd, &msg->scoped_name, &f->visible);
				if (err != GREMLINP_OK) {
					ctx->error = err;
					ctx->failed_source_idx = i;
					ctx->failed_field = fd;
					return err;
				}
			}
		}
	}

	return GREMLINP_OK;
}

/* ------------------------------------------------------------------------
 * Topological ordering — files by import graph, messages by field-ref graph.
 * Both use iterative DFS with 3-colour marking and emit in post-order
 * (leaves first), which is dependency-first for C codegen.
 * ------------------------------------------------------------------------ */

#define GREMLIND_T_WHITE 0
#define GREMLIND_T_GRAY  1
#define GREMLIND_T_BLACK 2

struct topo_frame {
	size_t node;		/* index into the nodes array being sorted */
	size_t next_edge;	/* next outgoing edge from `node` to visit */
};

enum gremlinp_parsing_error
gremlind_topo_sort_files(struct gremlind_resolve_context *ctx,
			 struct gremlind_file_order *out)
{
	out->items = NULL;
	out->count = 0;

	if (ctx == NULL || (ctx->files == NULL && ctx->n_sources > 0)) {
		if (ctx) ctx->error = GREMLINP_ERROR_NULL_POINTER;
		return GREMLINP_ERROR_NULL_POINTER;
	}
	if (ctx->n_sources == 0) return GREMLINP_OK;

	out->items = gremlind_arena_alloc(ctx->arena,
		ctx->n_sources * sizeof(*out->items));
	unsigned char *color = gremlind_arena_alloc(ctx->arena, ctx->n_sources);
	struct topo_frame *stack = gremlind_arena_alloc(ctx->arena,
		ctx->n_sources * sizeof(*stack));
	if (out->items == NULL || color == NULL || stack == NULL) {
		ctx->error = GREMLINP_ERROR_OUT_OF_MEMORY;
		return GREMLINP_ERROR_OUT_OF_MEMORY;
	}
	for (size_t i = 0; i < ctx->n_sources; i++) color[i] = GREMLIND_T_WHITE;

	for (size_t root = 0; root < ctx->n_sources; root++) {
		if (color[root] != GREMLIND_T_WHITE) continue;

		size_t top = 0;
		stack[top].node = root;
		stack[top].next_edge = 0;
		color[root] = GREMLIND_T_GRAY;

		while (1) {
			struct topo_frame *fr = &stack[top];
			struct gremlind_file *f = ctx->files[fr->node];

			if (f == NULL || fr->next_edge >= f->imports.count) {
				color[fr->node] = GREMLIND_T_BLACK;
				out->items[out->count++] = ctx->files[fr->node];
				if (top == 0) break;
				top--;
				continue;
			}

			struct gremlind_import *imp = &f->imports.items[fr->next_edge++];
			struct gremlind_file *target = imp->resolved;
			if (target == NULL) continue;

			size_t tidx = (size_t)-1;
			for (size_t i = 0; i < ctx->n_sources; i++) {
				if (ctx->files[i] == target) { tidx = i; break; }
			}
			if (tidx == (size_t)-1) continue;
			if (color[tidx] != GREMLIND_T_WHITE) continue;

			top++;
			stack[top].node = tidx;
			stack[top].next_edge = 0;
			color[tidx] = GREMLIND_T_GRAY;
		}
	}

	return GREMLINP_OK;
}

/*
 * Map a field's type_ref to an in-file message index (or -1 if the ref
 * doesn't point to a message in *this* file — builtins, enums,
 * cross-file types, and unresolved refs all return -1).
 */
static size_t
field_target_msg_idx(const struct gremlind_type_ref *tr,
		     const struct gremlind_messages *flat)
{
	const struct gremlind_message *target = NULL;
	if (tr->kind == GREMLIND_TYPE_REF_MESSAGE) {
		target = tr->u.message;
	} else if (tr->kind == GREMLIND_TYPE_REF_MAP &&
		   tr->u.map.value != NULL &&
		   tr->u.map.value->kind == GREMLIND_TYPE_REF_MESSAGE) {
		target = tr->u.map.value->u.message;
	}
	if (target == NULL) return (size_t)-1;

	if (target >= flat->items && target < flat->items + flat->count) {
		return (size_t)(target - flat->items);
	}
	return (size_t)-1;
}

enum gremlinp_parsing_error
gremlind_topo_sort_messages(struct gremlind_arena *arena,
			    struct gremlind_file *file,
			    struct gremlind_message_order *out)
{
	out->items = NULL;
	out->count = 0;
	out->predeclare = NULL;
	out->predeclare_count = 0;

	if (arena == NULL || file == NULL) return GREMLINP_ERROR_NULL_POINTER;
	if (file->messages.count == 0) return GREMLINP_OK;

	size_t n = file->messages.count;
	out->items = gremlind_arena_alloc(arena, n * sizeof(*out->items));
	unsigned char *color = gremlind_arena_alloc(arena, n);
	unsigned char *needs_pre = gremlind_arena_alloc(arena, n);
	struct topo_frame *stack = gremlind_arena_alloc(arena, n * sizeof(*stack));
	if (out->items == NULL || color == NULL ||
	    needs_pre == NULL || stack == NULL) {
		return GREMLINP_ERROR_OUT_OF_MEMORY;
	}
	for (size_t i = 0; i < n; i++) {
		color[i] = GREMLIND_T_WHITE;
		needs_pre[i] = 0;
	}

	for (size_t root = 0; root < n; root++) {
		if (color[root] != GREMLIND_T_WHITE) continue;

		size_t top = 0;
		stack[top].node = root;
		stack[top].next_edge = 0;
		color[root] = GREMLIND_T_GRAY;

		while (1) {
			struct topo_frame *fr = &stack[top];
			struct gremlind_message *m = &file->messages.items[fr->node];

			if (fr->next_edge >= m->fields.count) {
				color[fr->node] = GREMLIND_T_BLACK;
				out->items[out->count++] = m;
				if (top == 0) break;
				top--;
				continue;
			}

			struct gremlind_field *fd = &m->fields.items[fr->next_edge++];
			size_t tidx = field_target_msg_idx(&fd->type, &file->messages);
			if (tidx == (size_t)-1) continue;

			if (color[tidx] == GREMLIND_T_GRAY) {
				/* Back edge — tidx needs forward declaration:
				 * some later struct in items will reference it
				 * before its own definition. */
				needs_pre[tidx] = 1;
				continue;
			}
			if (color[tidx] == GREMLIND_T_BLACK) continue;

			top++;
			stack[top].node = tidx;
			stack[top].next_edge = 0;
			color[tidx] = GREMLIND_T_GRAY;
		}
	}

	size_t pre_count = 0;
	for (size_t i = 0; i < n; i++) if (needs_pre[i]) pre_count++;
	if (pre_count > 0) {
		out->predeclare = gremlind_arena_alloc(arena,
			pre_count * sizeof(*out->predeclare));
		if (out->predeclare == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		for (size_t i = 0; i < n; i++) {
			if (needs_pre[i]) {
				out->predeclare[out->predeclare_count++] =
					&file->messages.items[i];
			}
		}
	}

	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlind_link_imports(struct gremlind_resolve_context *ctx)
{
	if (ctx == NULL || (ctx->files == NULL && ctx->n_sources > 0)) {
		if (ctx) {
			ctx->error = GREMLINP_ERROR_NULL_POINTER;
		}
		return GREMLINP_ERROR_NULL_POINTER;
	}

	for (size_t i = 0; i < ctx->n_sources; i++) {
		struct gremlind_file *file = ctx->files[i];
		if (file == NULL) {
			continue;
		}
		for (size_t k = 0; k < file->imports.count; k++) {
			struct gremlind_import *imp = &file->imports.items[k];
			struct gremlind_file *target = lookup(ctx,
				imp->parsed.path_start,
				imp->parsed.path_length);
			if (target == NULL) {
				ctx->error = GREMLINP_ERROR_IMPORT_TARGET_NOT_FOUND;
				ctx->failed_source_idx = i;
				ctx->failed_import = imp;
				return GREMLINP_ERROR_IMPORT_TARGET_NOT_FOUND;
			}
			imp->resolved = target;
		}
	}

	return GREMLINP_OK;
}

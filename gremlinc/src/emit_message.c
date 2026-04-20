#include <string.h>
#include <stdio.h>

#include "gremlin.h"
#include "gremlinc/emit.h"
#include "gremlinc/naming.h"

#include "emit_common.h"

/*
 * Message emission orchestrator. Walks a descriptor, pre-claims every
 * emitted identifier in the global/member scopes, then hands each field
 * off to the family module that owns its wire shape:
 *
 *   - emit_scalar.c           scalar numerics + bool
 *   - emit_bytes.c            strings / bytes
 *   - emit_enum_ref.c         enum field references
 *   - emit_msg_ref.c          singular message references
 *   - emit_repeated_scalar.c  packed / unpacked repeated scalars
 *   - emit_repeated_bytes.c   repeated strings / bytes
 *   - emit_repeated_msg.c     repeated message references
 *   - emit_map.c              map fields (key/value pairs)
 *
 * New families plug in by adding a dispatch branch in each of the three
 * emit_* loops plus resolve_default_literal. See emit_common.h for the
 * per-family function signatures.
 */

/* ------------------------------------------------------------------------
 * Dispatchers — one call site per operation.  The only place in codegen
 * that knows which family owns a field.  `si` is NULL for enum-ref and
 * message-ref fields (they carry their type info on the type_ref's
 * target descriptor).
 * ------------------------------------------------------------------------ */

/* Predicate helpers reused across dispatchers. */
static bool
field_is_repeated_bytes(const struct gremlind_field *f)
{
	if (f->parsed.label != GREMLINP_FIELD_LABEL_REPEATED) return false;
	return f->type.kind == GREMLIND_TYPE_REF_BUILTIN &&
	       gremlinc_builtin_is_bytes(f->type.u.builtin.start,
					 f->type.u.builtin.length);
}

static bool
field_is_repeated_msg(const struct gremlind_field *f)
{
	return f->parsed.label == GREMLINP_FIELD_LABEL_REPEATED &&
	       f->type.kind == GREMLIND_TYPE_REF_MESSAGE;
}

static bool
field_is_bytes(const struct gremlind_field *f)
{
	return f->parsed.label != GREMLINP_FIELD_LABEL_REPEATED &&
	       f->type.kind == GREMLIND_TYPE_REF_BUILTIN &&
	       gremlinc_builtin_is_bytes(f->type.u.builtin.start,
					 f->type.u.builtin.length);
}

static bool
field_is_map(const struct gremlind_field *f)
{
	return f->type.kind == GREMLIND_TYPE_REF_MAP;
}

static bool
field_is_repeated_scalar(const struct gremlind_field *f)
{
	if (f->parsed.label != GREMLINP_FIELD_LABEL_REPEATED) return false;
	if (f->type.kind == GREMLIND_TYPE_REF_ENUM) return true;
	if (f->type.kind != GREMLIND_TYPE_REF_BUILTIN) return false;
	if (gremlinc_builtin_is_bytes(f->type.u.builtin.start,
				      f->type.u.builtin.length)) return false;
	return gremlinc_scalar_lookup(f->type.u.builtin.start,
				      f->type.u.builtin.length) != NULL;
}

static enum gremlinp_parsing_error
dispatch_size_field(struct gremlinc_writer *w,
		    struct gremlind_arena *arena,
		    const struct gremlind_field *f,
		    const struct scalar_info *si,
		    const char *fname,
		    const char *default_lit,
		    const struct map_slot_info *mkey,
		    const struct map_slot_info *mval)
{
	if (field_is_map(f)) {
		return gremlinc_map_emit_size_field(w, f, fname, mkey, mval);
	}
	if (field_is_repeated_bytes(f)) {
		return gremlinc_repeated_bytes_emit_size_field(w, f, fname);
	}
	if (field_is_repeated_msg(f)) {
		return gremlinc_repeated_msg_emit_size_field(w, f, fname);
	}
	if (field_is_repeated_scalar(f)) {
		return gremlinc_repeated_scalar_emit_size_field(w, f, fname);
	}
	if (f->type.kind == GREMLIND_TYPE_REF_MESSAGE) {
		return gremlinc_msg_ref_emit_size_field(w, f, fname);
	}
	if (f->type.kind == GREMLIND_TYPE_REF_ENUM) {
		return gremlinc_enum_ref_emit_size_field(w, f, fname, default_lit);
	}
	if (field_is_bytes(f)) {
		return gremlinc_bytes_emit_size_field(w, arena, f, fname);
	}
	return gremlinc_scalar_emit_size_field(w, f, si, fname, default_lit);
}

static enum gremlinp_parsing_error
dispatch_encode_field(struct gremlinc_writer *w,
		      struct gremlind_arena *arena,
		      const struct gremlind_field *f,
		      const struct scalar_info *si,
		      const char *fname,
		      const char *default_lit,
		      const struct map_slot_info *mkey,
		      const struct map_slot_info *mval)
{
	if (field_is_map(f)) {
		return gremlinc_map_emit_encode_field(w, f, fname, mkey, mval);
	}
	if (field_is_repeated_bytes(f)) {
		return gremlinc_repeated_bytes_emit_encode_field(w, f, fname);
	}
	if (field_is_repeated_msg(f)) {
		return gremlinc_repeated_msg_emit_encode_field(w, f, fname);
	}
	if (field_is_repeated_scalar(f)) {
		return gremlinc_repeated_scalar_emit_encode_field(w, f, fname);
	}
	if (f->type.kind == GREMLIND_TYPE_REF_MESSAGE) {
		return gremlinc_msg_ref_emit_encode_field(w, f, fname);
	}
	if (f->type.kind == GREMLIND_TYPE_REF_ENUM) {
		return gremlinc_enum_ref_emit_encode_field(w, f, fname, default_lit);
	}
	if (field_is_bytes(f)) {
		return gremlinc_bytes_emit_encode_field(w, arena, f, fname);
	}
	return gremlinc_scalar_emit_encode_field(w, f, si, fname, default_lit);
}

static enum gremlinp_parsing_error
dispatch_reader_arm(struct gremlinc_writer *w,
		    const struct gremlind_field *f,
		    const struct scalar_info *si,
		    const char *fname,
		    const char *packed_bool_fname)
{
	if (field_is_map(f)) {
		return gremlinc_map_emit_reader_arm(w, f, fname);
	}
	if (field_is_repeated_bytes(f)) {
		return gremlinc_repeated_bytes_emit_reader_arm(w, f, fname);
	}
	if (field_is_repeated_msg(f)) {
		return gremlinc_repeated_msg_emit_reader_arm(w, f, fname);
	}
	if (field_is_repeated_scalar(f)) {
		return gremlinc_repeated_scalar_emit_reader_arm(w, f, fname, packed_bool_fname);
	}
	if (f->type.kind == GREMLIND_TYPE_REF_MESSAGE) {
		return gremlinc_msg_ref_emit_reader_arm(w, f, fname);
	}
	if (f->type.kind == GREMLIND_TYPE_REF_ENUM) {
		return gremlinc_enum_ref_emit_reader_arm(w, f, fname);
	}
	if (field_is_bytes(f)) {
		return gremlinc_bytes_emit_reader_arm(w, f, fname);
	}
	return gremlinc_scalar_emit_reader_arm(w, f, si, fname);
}

static const char *
dispatch_resolve_default(struct gremlind_arena *arena,
			 const struct gremlind_field *f,
			 const struct scalar_info *si)
{
	if (field_is_repeated_bytes(f) || field_is_repeated_msg(f) ||
	    field_is_repeated_scalar(f) ||
	    field_is_map(f)) {
		/* Repeated / map have no default literal — count drives
		 * size/encode/decode guards. */
		return "0";
	}
	if (f->type.kind == GREMLIND_TYPE_REF_MESSAGE) {
		return gremlinc_msg_ref_default_literal();
	}
	if (f->type.kind == GREMLIND_TYPE_REF_ENUM) {
		return gremlinc_enum_ref_resolve_default(arena, f);
	}
	if (field_is_bytes(f)) {
		return gremlinc_bytes_resolve_default(arena, f);
	}
	return gremlinc_scalar_resolve_default(arena, f, si);
}

/* C type for a non-repeated field in the WRITER struct.  Message refs
 * use a pointer to the target's typedef; enum refs use the enum
 * typedef; bytes/string share `struct gremlin_bytes`; everything else
 * uses the scalar_info's c_type.  Repeated fields have their own
 * two-field layout handled inline in the struct emission. */
static const char *
writer_field_c_type(struct gremlind_arena *arena,
		    const struct gremlind_field *f,
		    const struct scalar_info *si)
{
	if (f->type.kind == GREMLIND_TYPE_REF_ENUM) {
		return f->type.u.enumeration->c_name;
	}
	if (f->type.kind == GREMLIND_TYPE_REF_MESSAGE) {
		const char *tname = f->type.u.message->c_name;
		size_t tl = strlen(tname);
		size_t needed = tl + 10;	/* "const " + " *" */
		char *out = gremlind_arena_alloc(arena, needed);
		if (out == NULL) return NULL;
		snprintf(out, needed, "const %s *", tname);
		return out;
	}
	if (field_is_bytes(f)) {
		return "struct gremlin_bytes";
	}
	return si->c_type;
}

/* C type for a non-repeated field in the READER struct.  Message refs
 * cache a zero-copy byte slice (`struct gremlin_bytes`).  Everything
 * else matches the writer's type. */
static const char *
reader_field_c_type(struct gremlind_arena *arena,
		    const struct gremlind_field *f,
		    const struct scalar_info *si)
{
	if (f->type.kind == GREMLIND_TYPE_REF_MESSAGE) {
		return "struct gremlin_bytes";
	}
	return writer_field_c_type(arena, f, si);
}

/* ------------------------------------------------------------------------
 * Name claiming helpers — shared across emission steps.
 * ------------------------------------------------------------------------ */

static const char *
field_cname(struct gremlinc_name_scope *member_scope,
	    const struct gremlind_field *f)
{
	/* Proto sources use either snake_case or camelCase for field
	 * names.  Normalise to snake_case before claiming so struct
	 * members are stylistically consistent across the generated
	 * code — `isUsernameDeleted` → `is_username_deleted`. */
	size_t snake_len = 0;
	const char *snake = gremlinc_to_snake_case(member_scope->arena,
					f->parsed.name_start,
					f->parsed.name_length,
					&snake_len);
	if (snake == NULL) return NULL;
	return gremlinc_name_scope_mangle(member_scope, snake, snake_len);
}

static const char *
claim_join2(struct gremlinc_name_scope *s, const char *a, const char *b)
{
	size_t la = strlen(a), lb = strlen(b), total = la + lb;
	char *raw = gremlind_arena_alloc(s->arena, total + 1);
	if (raw == NULL) return NULL;
	memcpy(raw, a, la);
	memcpy(raw + la, b, lb);
	raw[total] = '\0';
	return gremlinc_name_scope_mangle(s, raw, total);
}

static const char *
claim_join3(struct gremlinc_name_scope *s,
	    const char *a, const char *b, const char *c)
{
	size_t la = strlen(a), lb = strlen(b), lc = strlen(c);
	size_t total = la + lb + lc;
	char *raw = gremlind_arena_alloc(s->arena, total + 1);
	if (raw == NULL) return NULL;
	memcpy(raw, a, la);
	memcpy(raw + la, b, lb);
	memcpy(raw + la + lb, c, lc);
	raw[total] = '\0';
	return gremlinc_name_scope_mangle(s, raw, total);
}

/* ------------------------------------------------------------------------ */

enum gremlinp_parsing_error
gremlinc_emit_message_forward(struct gremlinc_writer *w,
			      const struct gremlind_message *m)
{
	if (w == NULL || m == NULL) return GREMLINP_ERROR_NULL_POINTER;
	if (m->c_name == NULL) return GREMLINP_ERROR_NULL_POINTER;

	enum gremlinp_parsing_error err;
	const char *tname = m->c_name;

	/* Typedefs for the struct tags — lets pointer-typed uses (`const
	 * M *x`, `M_reader *out`) compile without needing the full
	 * definition yet. */
	W("typedef struct "); W(tname); W(" "); W(tname); W(";\n");
	W("typedef struct "); W(tname); W("_reader "); W(tname); W("_reader;\n");

	/* Function forward decls — size / encode / reader_init.  Bodies
	 * are `static inline`; the compiler accepts a forward decl without
	 * body as long as the body appears later in the same TU. */
	/* `_size` is `noinline`: measurements show clang fully inlines
	 * child `_size` calls into every caller's loop body, making
	 * Level3_size 3× the code footprint of zig's equivalent (552 vs
	 * 174 insns) and blowing past L1 I-cache during the size-then-
	 * encode double traversal. `_encode` stays `static` — letting
	 * the compiler inline leaf encodes into their parent gives the
	 * fastest hot path for shallow chains. */
	W("__attribute__((noinline))\nstatic size_t "); W(tname); W("_size(const "); W(tname); W(" *m);\n");
	/* `_cached_size` is a thin inline getter that reads `m->_size`
	 * (populated by `_size`). Used by parent encoders to write length
	 * prefixes for sub-messages without re-running the recursive size
	 * walk. Forward-declared here so parent-encode function bodies
	 * can reference it even when the child's struct (and thus the
	 * inline body) hasn't been emitted yet — matters for mutual-
	 * recursive message types. */
	W("static inline size_t "); W(tname); W("_cached_size(const "); W(tname); W(" *m);\n");
	/* `_encode_at` is the real worker — takes (buf, off) by value and
	 * returns new offset, no struct round-trip. `_encode(writer*)` is
	 * a thin wrapper that calls `_encode_at` and syncs `writer.offset`
	 * once at the end. Recursive sub-message encode uses `_encode_at`
	 * directly so offset stays in a register across the whole tree. */
	W("static size_t "); W(tname); W("_encode_at(const "); W(tname);
	W(" *m, uint8_t * __restrict__ buf, size_t off);\n");
	W("static void   "); W(tname); W("_encode(const "); W(tname);
	W(" *m, struct gremlin_writer *w);\n");
	W("static inline enum gremlin_error "); W(tname); W("_reader_init(");
	W(tname); W("_reader *r, const uint8_t *src, size_t len);\n\n");

	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_emit_message(struct gremlinc_writer *w,
		      struct gremlinc_name_scope *scope,
		      const struct gremlind_message *m)
{
	if (w == NULL || scope == NULL || m == NULL) {
		return GREMLINP_ERROR_NULL_POINTER;
	}

	/* c_name is populated by the gremlinc_assign_c_names pre-pass;
	 * refuse if caller skipped it. This pre-pass makes forward-reference
	 * emission for message-typed and enum-typed fields trivial: a
	 * referrer reads the target's already-assigned c_name directly. */
	const char *tname = m->c_name;
	if (tname == NULL) return GREMLINP_ERROR_NULL_POINTER;

	/* Pre-claim every derived identifier we will emit in the global
	 * scope. Any future typedef that flat-joins to one of these gets
	 * bumped to `_1` by the mangler instead of silently clashing. The
	 * reader-side names chain off `reader_ty` so if the typedef itself
	 * was mangled (e.g. `_reader_1`) the related function names follow. */
	const char *size_fn	   = claim_join2(scope, tname, "_size");
	const char *encode_fn	   = claim_join2(scope, tname, "_encode");
	const char *reader_ty	   = claim_join2(scope, tname, "_reader");
	const char *reader_init_fn = claim_join2(scope, reader_ty, "_init");
	if (!size_fn || !encode_fn || !reader_ty || !reader_init_fn) {
		return GREMLINP_ERROR_OUT_OF_MEMORY;
	}

	/* Per-message member scope. Pre-register the reader struct's
	 * housekeeping fields so a proto field named "src" or "src_len"
	 * gets mangled instead of duplicate-struct-member'ing. */
	struct gremlinc_name_scope members;
	gremlinc_name_scope_init(&members, scope->arena);
	if (gremlinc_name_scope_mangle(&members, "src", 3) == NULL ||
	    gremlinc_name_scope_mangle(&members, "src_len", 7) == NULL ||
	    gremlinc_name_scope_mangle(&members, "_has", 4) == NULL) {
		return GREMLINP_ERROR_OUT_OF_MEMORY;
	}

	/* Pre-resolve each field's C name + scalar info + getter fn name +
	 * default literal. Stack arrays are sized for any realistic message. */
	if (m->fields.count > 1024) return GREMLINP_ERROR_OUT_OF_MEMORY;
	const char *fnames[1024];
	const char *getter_fns[1024];
	const char *defaults[1024];
	const struct scalar_info *sinfos[1024];
	/* Per-map-field state: classified key/value + generated entry type
	 * name.  Populated only for `field_is_map(fld)`; slots for other
	 * fields are undefined. */
	struct map_slot_info map_keys[1024];
	struct map_slot_info map_vals[1024];
	const char *entry_types[1024];
	/* Per-repeated-scalar-field state: mangled member name for the
	 * `<fname>_packed` bool the reader uses to discriminate packed vs
	 * unpacked wire form.  Populated only when
	 * `field_is_repeated_scalar(fld)`. */
	const char *packed_names[1024];
	for (size_t i = 0; i < m->fields.count; i++) {
		const struct gremlind_field *fld = &m->fields.items[i];
		fnames[i] = field_cname(&members, fld);
		if (fnames[i] == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		getter_fns[i] = claim_join3(scope, reader_ty, "_get_", fnames[i]);
		if (getter_fns[i] == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		entry_types[i] = NULL;
		packed_names[i] = NULL;
		if (field_is_repeated_scalar(fld)) {
			/* Reserve `<fname>_packed` in the member scope; the
			 * mangler takes care of collisions with proto fields
			 * named `<X>_packed`. */
			size_t fl = strlen(fnames[i]);
			char *buf = gremlind_arena_alloc(scope->arena, fl + 8);
			if (buf == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
			memcpy(buf, fnames[i], fl);
			memcpy(buf + fl, "_packed", 8);	/* includes NUL */
			packed_names[i] = gremlinc_name_scope_mangle(
				&members, buf, fl + 7);
			if (packed_names[i] == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		}
		if (field_is_map(fld)) {
			gremlinc_map_classify(scope->arena, fld,
					      &map_keys[i], &map_vals[i]);
			/* Claim `<tname>_<fname>_entry` in the global scope;
			 * mangler suffixes on collision. */
			char buf[512];
			int bn = snprintf(buf, sizeof buf, "%s_%s_entry",
					  tname, fnames[i]);
			if (bn <= 0 || bn >= (int)sizeof buf)
				return GREMLINP_ERROR_OUT_OF_MEMORY;
			entry_types[i] = gremlinc_name_scope_mangle(
				scope, buf, (size_t)bn);
			if (entry_types[i] == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		}
		sinfos[i] = NULL;
		if (fld->type.kind == GREMLIND_TYPE_REF_BUILTIN &&
		    !gremlinc_builtin_is_bytes(fld->type.u.builtin.start,
					       fld->type.u.builtin.length)) {
			sinfos[i] = gremlinc_scalar_lookup(
				fld->type.u.builtin.start,
				fld->type.u.builtin.length);
		}
		defaults[i] = dispatch_resolve_default(scope->arena, fld, sinfos[i]);
		/* NULL only on `[default = X]` conversion failure — refuse
		 * to emit rather than silently synthesize 0. */
		if (defaults[i] == NULL) {
			return GREMLINP_ERROR_INVALID_FIELD_VALUE;
		}
	}

	enum gremlinp_parsing_error err;

	/* ---- entry typedefs for map fields ---- */
	for (size_t i = 0; i < m->fields.count; i++) {
		if (!field_is_map(&m->fields.items[i])) continue;
		err = gremlinc_map_emit_entry_typedef(w, &m->fields.items[i],
			entry_types[i], &map_keys[i], &map_vals[i]);
		if (err != GREMLINP_OK) return err;
	}

	/* ---- writer struct ---- */
	W("typedef struct "); W(tname); W(" {\n");
	if (m->fields.count == 0) {
		/* C forbids empty structs; emit a placeholder. */
		W("\tchar _empty;\n");
	}
	for (size_t i = 0; i < m->fields.count; i++) {
		const struct gremlind_field *fld = &m->fields.items[i];
		if (field_is_map(fld)) {
			W("\t"); W(entry_types[i]); W("\t*"); W(fnames[i]); W(";\n");
			W("\tsize_t\t"); W(fnames[i]); W("_count;\n");
			continue;
		}
		if (field_is_repeated_bytes(fld)) {
			W("\tstruct gremlin_bytes\t*"); W(fnames[i]); W(";\n");
			W("\tsize_t\t"); W(fnames[i]); W("_count;\n");
			continue;
		}
		if (field_is_repeated_msg(fld)) {
			/* Array of pointers to caller-owned children. */
			W("\tconst "); W(fld->type.u.message->c_name);
			W(" * const\t*"); W(fnames[i]); W(";\n");
			W("\tsize_t\t"); W(fnames[i]); W("_count;\n");
			continue;
		}
		if (field_is_repeated_scalar(fld)) {
			/* Array of scalars / enums.  Element type is the
			 * field's usual C type. */
			const char *c_type = (fld->type.kind == GREMLIND_TYPE_REF_ENUM)
				? fld->type.u.enumeration->c_name
				: sinfos[i]->c_type;
			W("\t"); W(c_type); W("\t*"); W(fnames[i]); W(";\n");
			W("\tsize_t\t"); W(fnames[i]); W("_count;\n");
			continue;
		}
		const char *wt = writer_field_c_type(scope->arena, fld, sinfos[i]);
		if (wt == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		W("\t"); W(wt); W("\t"); W(fnames[i]); W(";\n");
	}
	/* Size cache — populated by `<tname>_size`, read by any parent's
	 * `_encode` when writing the length prefix for this sub-message.
	 * Avoids re-running the full recursive size traversal inside
	 * `_encode`. Placed at the end so designated-initializer callers
	 * (`.field = x`) still compile and default-zero the cache. */
	W("\tsize_t\t_size;\n");
	W("} "); W(tname); W(";\n\n");

	/* Inline cached-size accessor. Body is trivial `return m->_size`
	 * but needs the full struct to be visible — so it's defined here,
	 * right after the struct. The forward decl above (in
	 * `gremlinc_emit_message_forward`) lets earlier-emitted sibling
	 * encoders reference this function even when the struct wasn't
	 * yet defined at their emission site. */
	W("static inline size_t\n");
	W(tname); W("_cached_size(const "); W(tname); W(" *m)\n{\n");
	W("\treturn m->_size;\n}\n\n");

	/* ---- size function ----
	 *
	 * Single flat body: each field's contribution is emitted inline
	 * via dispatch_size_field into one shared accumulator `s`. A
	 * per-field-helper split was tried and consistently cost ~7% on
	 * the benchmarks even with `always_inline` — function-boundary
	 * codegen diverged from the flat form even after inlining. The
	 * flat form wins on perf and stays the codegen default.
	 *
	 * Cache write: cast-away-const preserves the public
	 * `_size(const X *m)` API while mutating the cache. UB for
	 * truly const-storage protos; in practice messages are built
	 * mutable then encoded. */
	W("__attribute__((noinline)) static size_t\n");
	W(size_fn); W("(const "); W(tname); W(" *m)\n{\n");
	W("\tsize_t s = 0;\n");
	if (m->fields.count == 0) {
		W("\t(void)m;\n");
	}
	for (size_t i = 0; i < m->fields.count; i++) {
		err = dispatch_size_field(w, scope->arena,
					  &m->fields.items[i], sinfos[i],
					  fnames[i], defaults[i],
					  &map_keys[i], &map_vals[i]);
		if (err != GREMLINP_OK) return err;
	}
	W("\t((" ); W(tname); W(" *)m)->_size = s;\n");
	W("\treturn s;\n}\n\n");

	/* ---- encode_at (worker) + encode (wrapper) ----
	 *
	 * `_encode_at` takes (buf, off) by value and returns the new off.
	 * Keeping offset in a register across the whole tree — nested
	 * sub-messages also use `_encode_at`, so no writer round-trip
	 * between levels.
	 *
	 * `_encode` stays as the stable public API: wraps `_encode_at`
	 * and syncs `w->offset` once.
	 *
	 * `_buf` marked `__restrict__`: tells the compiler `buf` doesn't
	 * alias the input message, letting it fold away otherwise-forced
	 * reloads between consecutive writes. */
	/* Flat encode body — same rationale as `_size`: per-field helper
	 * split cost ~7% on bench even with always_inline. */
	W("static size_t\n");
	W(tname); W("_encode_at(const "); W(tname);
	W(" *m, uint8_t * __restrict__ _buf, size_t _off)\n{\n");
	if (m->fields.count == 0) {
		W("\t(void)m;\n\t(void)_buf;\n");
	}
	for (size_t i = 0; i < m->fields.count; i++) {
		err = dispatch_encode_field(w, scope->arena,
					    &m->fields.items[i], sinfos[i],
					    fnames[i], defaults[i],
					    &map_keys[i], &map_vals[i]);
		if (err != GREMLINP_OK) return err;
	}
	W("\treturn _off;\n}\n\n");

	W("static void\n");
	W(encode_fn); W("(const "); W(tname); W(" *m, struct gremlin_writer *w)\n{\n");
	W("\tw->offset = "); W(tname); W("_encode_at(m, w->buf, w->offset);\n");
	W("}\n\n");

	/* ---- reader struct ----
	 * Per-field has-bits live in a `_has` sub-struct as `unsigned :1`
	 * bit-fields so their names (e.g. `_has.value`) don't collide with
	 * the proto field names on the same struct, AND N has-bits compress
	 * to ⌈N/8⌉ bytes instead of N bytes.  The sub-struct field `_has`
	 * is pre-registered in the member scope above.  Has-bits resolve
	 * the "absent on wire" vs "set to zero" ambiguity that sentinel
	 * defaults can't express. */
	W("typedef struct "); W(reader_ty); W(" {\n");
	W("\tconst uint8_t\t*src;\n");
	W("\tsize_t\t\t src_len;\n");
	if (m->fields.count > 0) {
		W("\tstruct {\n");
		for (size_t i = 0; i < m->fields.count; i++) {
			W("\t\tunsigned\t"); W(fnames[i]); W(" : 1;\n");
		}
		W("\t} _has;\n");
	}
	for (size_t i = 0; i < m->fields.count; i++) {
		const struct gremlind_field *fld = &m->fields.items[i];
		if (field_is_repeated_bytes(fld) ||
		    field_is_repeated_msg(fld) ||
		    field_is_map(fld)) {
			W("\tsize_t\t"); W(fnames[i]); W("_count;\n");
			W("\tsize_t\t"); W(fnames[i]); W("_first_offset;\n");
			continue;
		}
		if (field_is_repeated_scalar(fld)) {
			W("\tsize_t\t"); W(fnames[i]); W("_count;\n");
			W("\tsize_t\t"); W(fnames[i]); W("_first_offset;\n");
			W("\tbool\t"); W(packed_names[i]); W(";\n");
			continue;
		}
		const char *rt = reader_field_c_type(scope->arena, fld, sinfos[i]);
		if (rt == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		W("\t"); W(rt); W("\t"); W(fnames[i]); W(";\n");
	}
	W("} "); W(reader_ty); W(";\n\n");

	/* ---- reader init ---- */
	W("static inline enum gremlin_error\n");
	W(reader_init_fn); W("("); W(reader_ty); W(" *r, const uint8_t *src, size_t len)\n{\n");
	/* Compound literal zero-initializes every field including the
	 * `_has` bits. Declared defaults live in the getter's absent-path
	 * instead of being copied into the cache — the cache is only
	 * populated from wire data. */
	W("\t*r = ("); W(reader_ty); W("){ .src = src, .src_len = len };\n");
	W("\tsize_t offset = 0;\n");
	W("\twhile (offset < len) {\n");
	/* Read the tag via varint32 — protobuf tags fit in uint32 (field_num
	 * ≤ 2^29-1 << 3 | 7 < 2^32). Exact uint32 match against a codegen-
	 * computed packed constant gives a single-compare fast path. */
	W("\t\tstruct gremlin_varint32_decode_result t =\n"
	  "\t\t\tgremlin_varint32_decode(src + offset, len - offset);\n"
	  "\t\tif (t.error != GREMLIN_OK) return t.error;\n"
	  "\t\toffset += t.consumed;\n");
	for (size_t i = 0; i < m->fields.count; i++) {
		err = dispatch_reader_arm(w, &m->fields.items[i], sinfos[i],
					  fnames[i], packed_names[i]);
		if (err != GREMLINP_OK) return err;
	}
	/* Miss: unknown field_num, or field_num we know but wrong wire_type.
	 * Extract wire_type + field_num inline to preserve the same reject
	 * semantics tag_decode would have given us. */
	W("\t\tunsigned _wt = (unsigned)(t.value % 8u);\n"
	  "\t\tif (_wt == 6u || _wt == 7u) return GREMLIN_ERROR_INVALID_WIRE_TYPE;\n"
	  "\t\tuint32_t _fn = t.value / 8u;\n"
	  "\t\tif (_fn == 0u || _fn > GREMLIN_MAX_FIELD_NUM)\n"
	  "\t\t\treturn GREMLIN_ERROR_INVALID_FIELD_NUM;\n"
	  "\t\tstruct gremlin_skip_result sk =\n"
	  "\t\t\tgremlin_skip_data(src + offset, len - offset,\n"
	  "\t\t\t                  (enum gremlin_wire_type)_wt);\n"
	  "\t\tif (sk.error != GREMLIN_OK) return sk.error;\n"
	  "\t\toffset += sk.consumed;\n");
	W("\t}\n");
	W("\treturn GREMLIN_OK;\n}\n\n");

	/* ---- per-field getters ----
	 * Scalar / bytes / enum: returns the cached value by value (or
	 * declared default).  Message refs: caller provides a child
	 * reader; getter lazily inits it from the cached byte slice.
	 * Repeated: iterator + count accessor instead of a single getter. */
	for (size_t i = 0; i < m->fields.count; i++) {
		const struct gremlind_field *fld = &m->fields.items[i];
		if (field_is_repeated_bytes(fld)) {
			err = gremlinc_repeated_bytes_emit_getter(w, fld,
				reader_ty, fnames[i]);
			if (err != GREMLINP_OK) return err;
			continue;
		}
		if (field_is_repeated_msg(fld)) {
			err = gremlinc_repeated_msg_emit_getter(w, fld,
				reader_ty, fnames[i]);
			if (err != GREMLINP_OK) return err;
			continue;
		}
		if (field_is_map(fld)) {
			err = gremlinc_map_emit_getter(w, scope->arena, fld,
				reader_ty, fnames[i],
				&map_keys[i], &map_vals[i]);
			if (err != GREMLINP_OK) return err;
			continue;
		}
		if (field_is_repeated_scalar(fld)) {
			err = gremlinc_repeated_scalar_emit_getter(w, fld,
				reader_ty, fnames[i], packed_names[i]);
			if (err != GREMLINP_OK) return err;
			continue;
		}
		if (fld->type.kind == GREMLIND_TYPE_REF_MESSAGE) {
			const char *ttarget = fld->type.u.message->c_name;
			W("static inline enum gremlin_error\n");
			W(getter_fns[i]);
			W("(const "); W(reader_ty); W(" *r, "); W(ttarget);
			W("_reader *out)\n{\n");
			/* Absent / null parent → init child with an empty
			 * buffer so the caller still gets a valid reader
			 * (whose every getter returns defaults). */
			W("\tif (r == NULL || !r->_has."); W(fnames[i]); W(") {\n");
			W("\t\treturn "); W(ttarget); W("_reader_init(out, NULL, 0);\n");
			W("\t}\n");
			W("\treturn "); W(ttarget);
			W("_reader_init(out, r->"); W(fnames[i]); W(".data, r->");
			W(fnames[i]); W(".len);\n");
			W("}\n\n");
			continue;
		}
		const char *rt = reader_field_c_type(scope->arena, fld, sinfos[i]);
		if (rt == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		W("static inline "); W(rt); W("\n");
		W(getter_fns[i]);
		W("(const "); W(reader_ty); W(" *r)\n{\n");
		/* Combined NULL + presence check: treat "caller passed null"
		 * and "wire had no such field" the same way — return the
		 * field's declared default. Only return the cached wire value
		 * when it was actually decoded. */
		W("\tif (r == NULL || !r->_has."); W(fnames[i]); W(") return ");
		W(defaults[i]); W(";\n");
		W("\treturn r->"); W(fnames[i]); W(";\n}\n\n");
	}

	return GREMLINP_OK;
}

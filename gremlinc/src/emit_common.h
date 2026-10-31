#ifndef _GREMLINC_EMIT_COMMON_H_
#define _GREMLINC_EMIT_COMMON_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gremlinc/emit.h"
#include "gremlinc/writer.h"
#include "gremlinc/naming.h"

struct gremlinc_espec;

/*
 * Internal header shared by emit_message.c (orchestrator) and per-family
 * emit modules. Not installed — gremlinc's public API is
 * gremlinc_emit_message only. Each family module owns a coherent slice of
 * codegen logic: scalar numerics in emit_scalar.c, string/bytes in
 * emit_bytes.c. New families (enum refs, message refs, repeated, map,
 * oneof) slot in as additional files without touching the orchestrator.
 */

/* ------------------------------------------------------------------------
 * Scalar type descriptor
 * ------------------------------------------------------------------------ */

enum scalar_kind {
	SCALAR_INT32, SCALAR_INT64, SCALAR_UINT32, SCALAR_UINT64,
	SCALAR_SINT32, SCALAR_SINT64,
	SCALAR_FIXED32, SCALAR_FIXED64, SCALAR_SFIXED32, SCALAR_SFIXED64,
	SCALAR_DOUBLE, SCALAR_FLOAT, SCALAR_BOOL
};

/* Proto's builtin scalars are VARINT- or fixed-encoded numerics + bool.
 * `string` and `bytes` are separate length-delimited primitives and
 * are NOT listed in the scalar table — they are recognised via
 * `gremlinc_builtin_is_bytes` and emitted by the bytes family module.
 */

struct scalar_info {
	const char		*proto_name;
	size_t			 proto_name_len;
	const char		*c_type;
	unsigned		 wire_type;		/* runtime's wire-type enum value */
	const char		*wire_type_tok;		/* string token for emission */
	enum scalar_kind	 kind;
};

/* Lookup a scalar by the proto type-name span. NULL if the span isn't a
 * recognised builtin. */
const struct scalar_info	*gremlinc_scalar_lookup(const char *span, size_t len);

/* Family predicates — which wire-layout group a kind belongs to. */
bool				 gremlinc_kind_uses_varint32(enum scalar_kind k);

/* True iff the proto type-name span is `string` or `bytes` — the two
 * length-delimited primitive types that don't live in the scalar
 * table.  Both map to `struct gremlin_bytes` on the C side and share
 * identical wire semantics (only semantic intent differs). */
bool				 gremlinc_builtin_is_bytes(const char *span, size_t len);

/* C struct-layout alignment (in bytes) for a scalar kind's C type.
 * Used to sort struct fields by alignment descending so intermediate
 * padding is minimised. `struct gremlin_bytes` uses 8 (it contains a
 * pointer). */
unsigned			 gremlinc_kind_alignment(enum scalar_kind k);

/* Default-check RHS for scalar kinds without an explicit `[default]`. */
const char			*gremlinc_default_check_rhs(enum scalar_kind k);

/* ------------------------------------------------------------------------
 * Write helpers — used by every emit_* function. Each caller must have a
 * `struct gremlinc_writer *w` in scope and declare
 * `enum gremlinp_parsing_error err;` locally.
 * ------------------------------------------------------------------------ */

#define W(s)	do {							\
			err = gremlinc_write_cstr(w, (s));		\
			if (err != GREMLINP_OK) return err;		\
		} while (0)

#define WI(v)	do {							\
			err = gremlinc_write_i32(w, (int32_t)(v));	\
			if (err != GREMLINP_OK) return err;		\
		} while (0)

/* ACSL of the repeated-field iterators. `out_extra` is one more ensures for
 * _next and `child` the message it inits in `out`; both may be NULL. */
enum gremlinp_parsing_error	gremlinc_emit_iter_ok(struct gremlinc_writer *w,
							const char *iter_ok,
							const char *reader_ty,
							const char *fname,
							bool block);
enum gremlinp_parsing_error	gremlinc_emit_iter_count_spec(struct gremlinc_writer *w,
							const char *fname);
enum gremlinp_parsing_error	gremlinc_emit_iter_begin_spec(struct gremlinc_writer *w,
							const char *reader_ok,
							const char *iter_ok,
							const char *fname);
enum gremlinp_parsing_error	gremlinc_emit_iter_next_spec(struct gremlinc_writer *w,
							const char *iter_ok,
							const char *out_extra,
							const struct gremlind_message *child);
/* `    ensures` clause listing every gremlin_error a reader can return. */
enum gremlinp_parsing_error	gremlinc_emit_error_set(struct gremlinc_writer *w);
/* What a reader that a call left in `out` guarantees once it returned OK. */
enum gremlinp_parsing_error	gremlinc_emit_child_reader_ok(struct gremlinc_writer *w,
							const struct gremlind_message *child,
							const char *out);
/* The child's reader predicate, by the name the mangler gave it. */
enum gremlinp_parsing_error	gremlinc_emit_reader_ok_name(struct gremlinc_writer *w,
							const struct gremlind_message *m);
/* Annotation for a loop that scans tags forward; `block` when it may also
 * set block_end. */
enum gremlinp_parsing_error	gremlinc_emit_iter_scan_spec(struct gremlinc_writer *w,
							const char *indent,
							bool block);

/* ------------------------------------------------------------------------
 * Per-family emitters
 *
 * Each family implements the four operations a field needs:
 *
 *   size_field    — append to the M_size accumulator.
 *   encode_field  — emit tag + value write into the M_encode body.
 *   reader_arm    — emit the `if (t.value == <tag>) { ... }` arm in
 *                   M_reader_init.
 *   resolve_default — convert `[default]` option or type-zero to a C
 *                   literal usable everywhere (init, null-return,
 *                   comparison).
 *
 * The orchestrator dispatches on `kind` (via gremlinc_kind_is_bytes for
 * the current two families). Adding a new family = new .c file + a
 * dispatch branch here.
 * ------------------------------------------------------------------------ */

enum gremlinp_parsing_error	gremlinc_scalar_emit_size_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const struct scalar_info *si,
							const char *fname,
							const char *default_lit);
enum gremlinp_parsing_error	gremlinc_scalar_emit_encode_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const struct scalar_info *si,
							const char *fname,
							const char *default_lit);
enum gremlinp_parsing_error	gremlinc_scalar_emit_reader_arm(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const struct scalar_info *si,
							const char *fname);
const char			*gremlinc_scalar_resolve_default(struct gremlind_arena *arena,
							const struct gremlind_field *f,
							const struct scalar_info *si);

enum gremlinp_parsing_error	gremlinc_bytes_emit_size_field(struct gremlinc_writer *w,
							struct gremlind_arena *arena,
							const struct gremlind_field *f,
							const char *fname);
enum gremlinp_parsing_error	gremlinc_bytes_emit_encode_field(struct gremlinc_writer *w,
							struct gremlind_arena *arena,
							const struct gremlind_field *f,
							const char *fname);
enum gremlinp_parsing_error	gremlinc_bytes_emit_reader_arm(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname);
const char			*gremlinc_bytes_resolve_default(struct gremlind_arena *arena,
							const struct gremlind_field *f);

/*
 * Enum-ref family — fields whose type resolves to an enum descriptor.
 * Wire-identical to int32 (VARINT, 10-byte path for negatives), but the
 * C type is the enum's typedef name (from `e->c_name`) rather than a
 * built-in scalar.  No `scalar_info` is passed — the enum descriptor
 * itself carries the relevant info.
 */
enum gremlinp_parsing_error	gremlinc_enum_ref_emit_size_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname,
							const char *default_lit);
enum gremlinp_parsing_error	gremlinc_enum_ref_emit_encode_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname,
							const char *default_lit);
enum gremlinp_parsing_error	gremlinc_enum_ref_emit_reader_arm(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname);
const char			*gremlinc_enum_ref_resolve_default(struct gremlind_arena *arena,
							const struct gremlind_field *f);

/*
 * Message-ref family — fields whose type resolves to another message.
 * Wire type LEN_PREFIX (the child's serialized bytes).  The reader
 * caches `(data, len)` slices into the source buffer; the getter
 * takes a caller-provided child reader and runs its `_reader_init`
 * lazily.
 */
enum gremlinp_parsing_error	gremlinc_msg_ref_emit_size_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname,
							const struct gremlinc_espec *sp,
							size_t fi);
enum gremlinp_parsing_error	gremlinc_msg_ref_emit_encode_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname,
							const struct gremlinc_espec *sp,
							size_t fi);
enum gremlinp_parsing_error	gremlinc_msg_ref_emit_reader_arm(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname);
const char			*gremlinc_msg_ref_default_literal(void);

/*
 * Repeated-bytes family — `repeated string` / `repeated bytes`.  Each
 * element is length-delimited; encode loops per slot, reader caches
 * count + first_offset with a lazy iterator (begin + next).  Sibling
 * modules (`emit_repeated_msg.c`, packed scalars) will land later for
 * their respective element kinds.
 */
enum gremlinp_parsing_error	gremlinc_repeated_bytes_emit_size_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname,
							const struct gremlinc_espec *sp,
							size_t fi);
enum gremlinp_parsing_error	gremlinc_repeated_bytes_emit_encode_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname,
							const struct gremlinc_espec *sp,
							size_t fi);
enum gremlinp_parsing_error	gremlinc_repeated_bytes_emit_reader_arm(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname);
enum gremlinp_parsing_error	gremlinc_repeated_bytes_emit_getter(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *reader_ty,
							const char *reader_ok,
							const char *iter_ok,
							const char *fname);

/*
 * Repeated-message family — `repeated <Target>`.  Same wire shape as
 * repeated bytes (each element LEN_PREFIX), different C handling:
 * writer stores array of pointers, iterator next() inits a child
 * reader from each element's byte slice.
 */
enum gremlinp_parsing_error	gremlinc_repeated_msg_emit_size_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname,
							const struct gremlinc_espec *sp,
							size_t fi);
enum gremlinp_parsing_error	gremlinc_repeated_msg_emit_encode_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname,
							const struct gremlinc_espec *sp,
							size_t fi);
enum gremlinp_parsing_error	gremlinc_repeated_msg_emit_reader_arm(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname);
enum gremlinp_parsing_error	gremlinc_repeated_msg_emit_getter(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *reader_ty,
							const char *reader_ok,
							const char *iter_ok,
							const char *fname);

/*
 * Map family — `map<K, V>`.  Each entry on the wire is a LEN_PREFIX
 * nested message with key @ field 1 + value @ field 2.  Writer uses a
 * generated entry typedef + flat array; reader's iterator `next()`
 * takes two out-params and returns both key and value per call.
 */
enum map_slot_kind {
	GREMLINC_MS_SCALAR_VARINT,
	GREMLINC_MS_SCALAR_FIXED32,
	GREMLINC_MS_SCALAR_FIXED64,
	GREMLINC_MS_BYTES,
	GREMLINC_MS_ENUM,
	GREMLINC_MS_MESSAGE,
};

struct map_slot_info {
	enum map_slot_kind		 kind;
	const struct scalar_info	*scalar;	/* scalar kinds only */
	const struct gremlind_enum	*enum_ref;	/* ENUM kind only */
	const struct gremlind_message	*msg_ref;	/* MESSAGE kind only */
	const char			*c_type;	/* as written in entry */
};

void				 gremlinc_map_classify(struct gremlind_arena *arena,
						       const struct gremlind_field *f,
						       struct map_slot_info *out_key,
						       struct map_slot_info *out_value);

enum gremlinp_parsing_error	gremlinc_map_emit_entry_typedef(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *entry_type,
							const struct map_slot_info *key,
							const struct map_slot_info *val);
enum gremlinp_parsing_error	gremlinc_map_emit_size_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname,
							const struct map_slot_info *key,
							const struct map_slot_info *val);
enum gremlinp_parsing_error	gremlinc_map_emit_encode_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname,
							const struct map_slot_info *key,
							const struct map_slot_info *val);
enum gremlinp_parsing_error	gremlinc_map_emit_reader_arm(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname);
enum gremlinp_parsing_error	gremlinc_map_emit_getter(struct gremlinc_writer *w,
							struct gremlind_arena *arena,
							const struct gremlind_field *f,
							const char *reader_ty,
							const char *reader_ok,
							const char *iter_ok,
							const char *fname,
							const struct map_slot_info *key,
							const struct map_slot_info *val);

/*
 * Repeated-scalar family — `repeated <numeric>`, `repeated bool`,
 * `repeated <enum>`.  Supports both packed and unpacked wire forms.
 * Writer emits unpacked for count==1 (saves length byte), packed for
 * count>=2.  Reader rejects mixed packed/unpacked within a single
 * field (first occurrence sets `<fname>_packed`, subsequent occurrences
 * must match — mismatch → GREMLIN_ERROR_INVALID_WIRE_TYPE).
 */
enum gremlinp_parsing_error	gremlinc_repeated_scalar_emit_size_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname,
							const struct gremlinc_espec *sp,
							size_t fi);
enum gremlinp_parsing_error	gremlinc_repeated_scalar_emit_encode_field(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname,
							const struct gremlinc_espec *sp,
							size_t fi);
enum gremlinp_parsing_error	gremlinc_repeated_scalar_emit_reader_arm(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *fname,
							const char *packed_bool_fname);
enum gremlinp_parsing_error	gremlinc_repeated_scalar_emit_getter(struct gremlinc_writer *w,
							const struct gremlind_field *f,
							const char *reader_ty,
							const char *reader_ok,
							const char *iter_ok,
							const char *fname,
							const char *packed_bool_fname);

/* ------------------------------------------------------------------------
 * Write-side ACSL (emit_encode_spec.c).  Every call is a no-op when `sp`
 * is NULL, i.e. for a message outside the supported subset.
 * ------------------------------------------------------------------------ */

enum gremlinc_espec_hook_kind {
	GREMLINC_HOOK_MSG_SIZE_CHILD,
	GREMLINC_HOOK_MSG_ENCODE_CHILD,
	GREMLINC_HOOK_REPMSG_BODY,
	GREMLINC_HOOK_REPBYTES_BODY,
	GREMLINC_HOOK_REPMSG_SIZE_CHILD,
	GREMLINC_HOOK_REPMSG_NULL,
	GREMLINC_HOOK_REPMSG_ENCODE_CHILD,
	GREMLINC_HOOK_PACKED_PAYLOAD,
	GREMLINC_HOOK_PACKED_ELEMS,
	GREMLINC_HOOK_PACKED_ELEM
};

enum gremlinp_parsing_error	gremlinc_espec_begin(struct gremlinc_encode_plan *plan,
							struct gremlinc_name_scope *scope,
							const struct gremlind_message *m,
							const char *const *fnames,
							const char *const *defaults,
							struct gremlinc_espec **out);
enum gremlinp_parsing_error	gremlinc_espec_emit_logic(struct gremlinc_writer *w,
							struct gremlinc_name_scope *scope,
							struct gremlinc_espec *sp);
enum gremlinp_parsing_error	gremlinc_espec_cached_size_contract(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp);
enum gremlinp_parsing_error	gremlinc_espec_size_contract(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp);
enum gremlinp_parsing_error	gremlinc_espec_size_start(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp);
enum gremlinp_parsing_error	gremlinc_espec_size_field_before(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp,
							size_t fi);
enum gremlinp_parsing_error	gremlinc_espec_size_field_after(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp,
							size_t fi);
enum gremlinp_parsing_error	gremlinc_espec_size_before_write(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp);
enum gremlinp_parsing_error	gremlinc_espec_size_after_write(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp);
enum gremlinp_parsing_error	gremlinc_espec_encode_contract(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp);
enum gremlinp_parsing_error	gremlinc_espec_encode_start(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp);
enum gremlinp_parsing_error	gremlinc_espec_encode_field_before(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp,
							size_t fi);
enum gremlinp_parsing_error	gremlinc_espec_encode_field_after(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp,
							size_t fi);
enum gremlinp_parsing_error	gremlinc_espec_encode_end(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp);
enum gremlinp_parsing_error	gremlinc_espec_encode_wrapper_contract(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp);
enum gremlinp_parsing_error	gremlinc_espec_hook(struct gremlinc_writer *w,
							const struct gremlinc_espec *sp,
							size_t fi,
							enum gremlinc_espec_hook_kind h);

/* Claim <a><b> in s, suffixed past anything in s or reserved: for a name the
 * contracts add (a predicate, a ghost), which gives way to every public name
 * of the run, even one a later file emits. */
const char			*gremlinc_claim_avoiding(struct gremlinc_name_scope *s,
							 const struct gremlinc_name_scope *reserved,
							 const char *a, const char *b);

#endif /* !_GREMLINC_EMIT_COMMON_H_ */

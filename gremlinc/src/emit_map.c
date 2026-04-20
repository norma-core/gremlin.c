#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gremlin.h"

#include "emit_common.h"

/*
 * `map<K, V>` emission.
 *
 * On the wire, a proto map is sugar for `repeated <Entry>` where
 * Entry is a generated message with `key` at field 1 and `value` at
 * field 2.  Each entry is LEN_PREFIX-encoded.
 *
 * Writer: each map field gets a generated entry typedef
 * `<tname>_<fname>_entry { <K> key; <V> value; }` plus a flat array
 * + count (mirrors the repeated-bytes / repeated-msg shape).
 *
 * Reader: caches count + first_offset only; the per-field iterator's
 * `next()` takes two out-params (`<K> *out_key`, `<V> *out_value`),
 * decodes the outer LEN_PREFIX entry, walks its inner tags
 * (field 1 = key, field 2 = value), and populates both outs with
 * decoded values or type defaults.
 *
 * Key constraints (per proto spec): any integer scalar or `bool` or
 * `string`.  Float / double / bytes / enum / message are illegal as
 * keys.  Value may be any type we otherwise emit (scalar, bytes,
 * string, enum, message).
 */

#define WT_VARINT_NUM     0u
#define WT_FIXED64_NUM    1u
#define WT_LEN_PREFIX_NUM 2u
#define WT_FIXED32_NUM    5u

/* Type classification — enum + struct live in emit_common.h. */

/* Classify a map key/value's resolved type into its wire / C shape.
 * The parser rejects proto-invalid map keys/values (float/double keys,
 * message/enum/map as key, map-as-value) before resolve runs, so this
 * function only sees the well-formed cases. */
static void
classify_slot(struct gremlind_arena *arena,
	      const struct gremlind_type_ref *t,
	      struct map_slot_info *out)
{
	memset(out, 0, sizeof *out);

	if (t->kind == GREMLIND_TYPE_REF_BUILTIN) {
		if (gremlinc_builtin_is_bytes(t->u.builtin.start, t->u.builtin.length)) {
			out->kind = GREMLINC_MS_BYTES;
			out->c_type = "struct gremlin_bytes";
			return;
		}
		const struct scalar_info *si = gremlinc_scalar_lookup(
			t->u.builtin.start, t->u.builtin.length);
		out->scalar = si;
		out->c_type = si->c_type;
		switch (si->wire_type) {
		case WT_VARINT_NUM:  out->kind = GREMLINC_MS_SCALAR_VARINT;  return;
		case WT_FIXED32_NUM: out->kind = GREMLINC_MS_SCALAR_FIXED32; return;
		case WT_FIXED64_NUM: out->kind = GREMLINC_MS_SCALAR_FIXED64; return;
		}
		return;
	}
	if (t->kind == GREMLIND_TYPE_REF_ENUM) {
		out->kind = GREMLINC_MS_ENUM;
		out->enum_ref = t->u.enumeration;
		out->c_type = t->u.enumeration->c_name;
		return;
	}
	if (t->kind == GREMLIND_TYPE_REF_MESSAGE) {
		out->kind = GREMLINC_MS_MESSAGE;
		out->msg_ref = t->u.message;
		const char *tn = t->u.message->c_name;
		size_t tl = strlen(tn);
		size_t need = tl + 10;
		char *buf = gremlind_arena_alloc(arena, need);
		snprintf(buf, need, "const %s *", tn);
		out->c_type = buf;
	}
}

void
gremlinc_map_classify(struct gremlind_arena *arena,
		      const struct gremlind_field *f,
		      struct map_slot_info *out_key,
		      struct map_slot_info *out_value)
{
	classify_slot(arena, f->type.u.map.key,   out_key);
	classify_slot(arena, f->type.u.map.value, out_value);
}

/* Packed tag constant for the inner key (field 1) or value (field 2)
 * given the slot kind.  Wire type is implied by the kind. */
static uint32_t
slot_inner_tag(int field_num, const struct map_slot_info *s)
{
	unsigned wt = 0;
	switch (s->kind) {
	case GREMLINC_MS_SCALAR_VARINT:
	case GREMLINC_MS_ENUM:
		wt = WT_VARINT_NUM; break;
	case GREMLINC_MS_SCALAR_FIXED32:	wt = WT_FIXED32_NUM; break;
	case GREMLINC_MS_SCALAR_FIXED64:	wt = WT_FIXED64_NUM; break;
	case GREMLINC_MS_BYTES:
	case GREMLINC_MS_MESSAGE:	wt = WT_LEN_PREFIX_NUM; break;
	}
	return ((uint32_t)field_num << 3) | wt;
}

/* ------------------------------------------------------------------------
 * Size contributions — emit inline expression that evaluates to the
 * number of bytes the key or value occupies on the wire (including its
 * own tag byte).
 * ------------------------------------------------------------------------ */

static enum gremlinp_parsing_error
emit_slot_size_contrib(struct gremlinc_writer *w,
		       int field_num,
		       const struct map_slot_info *s,
		       const char *acc /* accumulator var, e.g. "inner" */,
		       const char *ref /* entry ref, e.g. "m->ages[_i].key" */)
{
	enum gremlinp_parsing_error err;
	uint32_t tag = slot_inner_tag(field_num, s);
	unsigned tag_bytes = (unsigned)gremlin_varint_size(tag);

	W("\t\t"); W(acc); W(" += "); WI((int32_t)tag_bytes);
	switch (s->kind) {
	case GREMLINC_MS_SCALAR_VARINT:
		switch (s->scalar->kind) {
		case SCALAR_INT32:
			W(" + gremlin_varint_size((uint64_t)(int64_t)"); W(ref); W(");\n");
			break;
		case SCALAR_INT64:
			W(" + gremlin_varint_size((uint64_t)"); W(ref); W(");\n");
			break;
		case SCALAR_UINT32:
			W(" + gremlin_varint32_size("); W(ref); W(");\n");
			break;
		case SCALAR_UINT64:
			W(" + gremlin_varint_size("); W(ref); W(");\n");
			break;
		case SCALAR_SINT32:
			W(" + gremlin_varint32_size(gremlin_zigzag32("); W(ref); W("));\n");
			break;
		case SCALAR_SINT64:
			W(" + gremlin_varint_size(gremlin_zigzag64("); W(ref); W("));\n");
			break;
		case SCALAR_BOOL:
			W(" + 1;\n");
			break;
		default: W(";\n"); break;
		}
		break;
	case GREMLINC_MS_SCALAR_FIXED32:	W(" + 4;\n"); break;
	case GREMLINC_MS_SCALAR_FIXED64:	W(" + 8;\n"); break;
	case GREMLINC_MS_BYTES:
		W(" + gremlin_varint_size("); W(ref); W(".len) + "); W(ref); W(".len;\n");
		break;
	case GREMLINC_MS_ENUM:
		W(" + gremlin_varint_size((uint64_t)(int64_t)"); W(ref); W(");\n");
		break;
	case GREMLINC_MS_MESSAGE: {
		/* Compute child size once above this line; the ref holds
		 * the pointer.  Caller pre-binds `_<entry>_cs` locally. */
		const char *tname = s->msg_ref->c_name;
		W(" + gremlin_varint_size(");
		W("("); W(ref); W(" != NULL ? "); W(tname); W("_size("); W(ref); W(") : 0)");
		W(") + "); W("("); W(ref); W(" != NULL ? "); W(tname);
		W("_size("); W(ref); W(") : 0);\n");
		break;
	}
	}
	return GREMLINP_OK;
}

static enum gremlinp_parsing_error
emit_slot_encode(struct gremlinc_writer *w,
		 int field_num,
		 const struct map_slot_info *s,
		 const char *ref)
{
	enum gremlinp_parsing_error err;
	uint32_t tag = slot_inner_tag(field_num, s);

	W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); WI((int32_t)tag); W("u);\n");
	switch (s->kind) {
	case GREMLINC_MS_SCALAR_VARINT:
		switch (s->scalar->kind) {
		case SCALAR_INT32:
			W("\t\t_off = gremlin_varint_encode_at(_buf, _off, (uint64_t)(int64_t)"); W(ref); W(");\n");
			break;
		case SCALAR_INT64:
			W("\t\t_off = gremlin_varint_encode_at(_buf, _off, (uint64_t)"); W(ref); W(");\n");
			break;
		case SCALAR_UINT32:
			W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); W(ref); W(");\n");
			break;
		case SCALAR_UINT64:
			W("\t\t_off = gremlin_varint_encode_at(_buf, _off, "); W(ref); W(");\n");
			break;
		case SCALAR_SINT32:
			W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, gremlin_zigzag32("); W(ref); W("));\n");
			break;
		case SCALAR_SINT64:
			W("\t\t_off = gremlin_varint_encode_at(_buf, _off, gremlin_zigzag64("); W(ref); W("));\n");
			break;
		case SCALAR_BOOL:
			W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); W(ref); W(" ? 1u : 0u);\n");
			break;
		default: break;
		}
		break;
	case GREMLINC_MS_SCALAR_FIXED32:
		switch (s->scalar->kind) {
		case SCALAR_FIXED32:
			W("\t\t_off = gremlin_fixed32_encode_at(_buf, _off, "); W(ref); W(");\n");
			break;
		case SCALAR_SFIXED32:
			W("\t\t_off = gremlin_fixed32_encode_at(_buf, _off, (uint32_t)"); W(ref); W(");\n");
			break;
		case SCALAR_FLOAT:
			W("\t\t_off = gremlin_fixed32_encode_at(_buf, _off, gremlin_f32_bits("); W(ref); W("));\n");
			break;
		default: break;
		}
		break;
	case GREMLINC_MS_SCALAR_FIXED64:
		switch (s->scalar->kind) {
		case SCALAR_FIXED64:
			W("\t\t_off = gremlin_fixed64_encode_at(_buf, _off, "); W(ref); W(");\n");
			break;
		case SCALAR_SFIXED64:
			W("\t\t_off = gremlin_fixed64_encode_at(_buf, _off, (uint64_t)"); W(ref); W(");\n");
			break;
		case SCALAR_DOUBLE:
			W("\t\t_off = gremlin_fixed64_encode_at(_buf, _off, gremlin_f64_bits("); W(ref); W("));\n");
			break;
		default: break;
		}
		break;
	case GREMLINC_MS_BYTES:
		W("\t\t_off = gremlin_varint_encode_at(_buf, _off, "); W(ref); W(".len);\n");
		W("\t\t_off = gremlin_write_bytes_at(_buf, _off, "); W(ref); W(".data, "); W(ref); W(".len);\n");
		break;
	case GREMLINC_MS_ENUM:
		W("\t\t_off = gremlin_varint_encode_at(_buf, _off, (uint64_t)(int64_t)"); W(ref); W(");\n");
		break;
	case GREMLINC_MS_MESSAGE: {
		const char *tname = s->msg_ref->c_name;
		/* Length prefix via `<tname>_cached_size` (forward-decl'd
		 * inline getter) — reads the `_size` cache on the value
		 * message without requiring its struct to be defined at
		 * this emission site. Recurse via `_encode_at`. */
		W("\t\t_off = gremlin_varint_encode_at(_buf, _off, "); W(ref); W(" != NULL ? ");
		W(tname); W("_cached_size("); W(ref); W(") : 0);\n");
		W("\t\tif ("); W(ref); W(" != NULL) {\n");
		W("\t\t\t_off = "); W(tname); W("_encode_at("); W(ref); W(", _buf, _off);\n");
		W("\t\t}\n");
		break;
	}
	}
	return GREMLINP_OK;
}

/* Inside `next()`, decode the current inner tag (already consumed into
 * local `t`) whose payload starts at `offset` — write the decoded
 * value into the out-param and advance offset. */
static enum gremlinp_parsing_error
emit_slot_decode(struct gremlinc_writer *w,
		 const struct map_slot_info *s,
		 const char *outref /* e.g. "(*out_key)" */)
{
	enum gremlinp_parsing_error err;
	switch (s->kind) {
	case GREMLINC_MS_SCALAR_VARINT:
		W("\t\t\tstruct gremlin_varint_decode_result d =\n"
		  "\t\t\t\tgremlin_varint_decode(ebuf + eoff, elen - eoff);\n"
		  "\t\t\tif (d.error != GREMLIN_OK) return d.error;\n"
		  "\t\t\teoff += d.consumed;\n");
		switch (s->scalar->kind) {
		case SCALAR_INT32:
			W("\t\t\t"); W(outref); W(" = (int32_t)(uint32_t)d.value;\n");
			break;
		case SCALAR_INT64:
			W("\t\t\t"); W(outref); W(" = (int64_t)d.value;\n");
			break;
		case SCALAR_UINT32:
			W("\t\t\t"); W(outref); W(" = (uint32_t)d.value;\n");
			break;
		case SCALAR_UINT64:
			W("\t\t\t"); W(outref); W(" = d.value;\n");
			break;
		case SCALAR_SINT32:
			W("\t\t\t"); W(outref); W(" = gremlin_unzigzag32((uint32_t)d.value);\n");
			break;
		case SCALAR_SINT64:
			W("\t\t\t"); W(outref); W(" = gremlin_unzigzag64(d.value);\n");
			break;
		case SCALAR_BOOL:
			W("\t\t\t"); W(outref); W(" = (d.value != 0);\n");
			break;
		default: break;
		}
		break;
	case GREMLINC_MS_SCALAR_FIXED32:
		W("\t\t\tstruct gremlin_fixed32_decode_result d =\n"
		  "\t\t\t\tgremlin_fixed32_decode(ebuf + eoff, elen - eoff);\n"
		  "\t\t\tif (d.error != GREMLIN_OK) return d.error;\n"
		  "\t\t\teoff += 4;\n");
		switch (s->scalar->kind) {
		case SCALAR_FIXED32:
			W("\t\t\t"); W(outref); W(" = d.value;\n");
			break;
		case SCALAR_SFIXED32:
			W("\t\t\t"); W(outref); W(" = (int32_t)d.value;\n");
			break;
		case SCALAR_FLOAT:
			W("\t\t\t"); W(outref); W(" = gremlin_f32_from_bits(d.value);\n");
			break;
		default: break;
		}
		break;
	case GREMLINC_MS_SCALAR_FIXED64:
		W("\t\t\tstruct gremlin_fixed64_decode_result d =\n"
		  "\t\t\t\tgremlin_fixed64_decode(ebuf + eoff, elen - eoff);\n"
		  "\t\t\tif (d.error != GREMLIN_OK) return d.error;\n"
		  "\t\t\teoff += 8;\n");
		switch (s->scalar->kind) {
		case SCALAR_FIXED64:
			W("\t\t\t"); W(outref); W(" = d.value;\n");
			break;
		case SCALAR_SFIXED64:
			W("\t\t\t"); W(outref); W(" = (int64_t)d.value;\n");
			break;
		case SCALAR_DOUBLE:
			W("\t\t\t"); W(outref); W(" = gremlin_f64_from_bits(d.value);\n");
			break;
		default: break;
		}
		break;
	case GREMLINC_MS_BYTES:
		W("\t\t\tstruct gremlin_bytes_decode_result d =\n"
		  "\t\t\t\tgremlin_bytes_decode(ebuf + eoff, elen - eoff);\n"
		  "\t\t\tif (d.error != GREMLIN_OK) return d.error;\n"
		  "\t\t\teoff += d.consumed;\n"
		  "\t\t\t"); W(outref); W(" = d.bytes;\n");
		break;
	case GREMLINC_MS_ENUM:
		W("\t\t\tstruct gremlin_varint_decode_result d =\n"
		  "\t\t\t\tgremlin_varint_decode(ebuf + eoff, elen - eoff);\n"
		  "\t\t\tif (d.error != GREMLIN_OK) return d.error;\n"
		  "\t\t\teoff += d.consumed;\n"
		  "\t\t\t"); W(outref); W(" = ("); W(s->enum_ref->c_name);
		W(")(int32_t)(uint32_t)d.value;\n");
		break;
	case GREMLINC_MS_MESSAGE: {
		const char *tname = s->msg_ref->c_name;
		W("\t\t\tstruct gremlin_bytes_decode_result d =\n"
		  "\t\t\t\tgremlin_bytes_decode(ebuf + eoff, elen - eoff);\n"
		  "\t\t\tif (d.error != GREMLIN_OK) return d.error;\n"
		  "\t\t\teoff += d.consumed;\n"
		  "\t\t\tenum gremlin_error _ie = "); W(tname);
		W("_reader_init("); W(outref); W(", d.bytes.data, d.bytes.len);\n"
		  "\t\t\tif (_ie != GREMLIN_OK) return _ie;\n");
		break;
	}
	}
	return GREMLINP_OK;
}

static const char *
slot_default_literal(const struct map_slot_info *s)
{
	switch (s->kind) {
	case GREMLINC_MS_SCALAR_VARINT:
	case GREMLINC_MS_SCALAR_FIXED32:
	case GREMLINC_MS_SCALAR_FIXED64:
		return gremlinc_default_check_rhs(s->scalar->kind);
	case GREMLINC_MS_BYTES:		return "(struct gremlin_bytes){ NULL, 0 }";
	case GREMLINC_MS_ENUM:		return "0";
	case GREMLINC_MS_MESSAGE:	return "";	/* caller handles — see next() */
	}
	return "0";
}

/* Accessor: returns the C type of a slot in the entry struct, suitable
 * for use as a by-value out-param type (e.g. int32_t, struct gremlin_bytes,
 * enum typedef).  For message values we return `<Target>_reader *`. */
static const char *
slot_out_param_type(struct gremlind_arena *arena, const struct map_slot_info *s)
{
	if (s->kind == GREMLINC_MS_MESSAGE) {
		const char *tn = s->msg_ref->c_name;
		size_t tl = strlen(tn);
		size_t need = tl + 10;
		char *out = gremlind_arena_alloc(arena, need);
		if (out == NULL) return NULL;
		snprintf(out, need, "%s_reader", tn);
		return out;
	}
	return s->c_type;
}

/* ------------------------------------------------------------------------
 * Public emission entry points
 * ------------------------------------------------------------------------ */

enum gremlinp_parsing_error
gremlinc_map_emit_entry_typedef(struct gremlinc_writer *w,
				const struct gremlind_field *f,
				const char *entry_type,
				const struct map_slot_info *key,
				const struct map_slot_info *val)
{
	enum gremlinp_parsing_error err;
	(void)f;
	W("typedef struct "); W(entry_type); W(" {\n");
	W("\t"); W(key->c_type); W("\tkey;\n");
	W("\t"); W(val->c_type); W("\tvalue;\n");
	W("} "); W(entry_type); W(";\n\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_map_emit_size_field(struct gremlinc_writer *w,
			     const struct gremlind_field *f,
			     const char *fname,
			     const struct map_slot_info *key,
			     const struct map_slot_info *val)
{
	enum gremlinp_parsing_error err;
	uint32_t outer_tag = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;
	unsigned outer_tag_bytes = (unsigned)gremlin_varint_size(outer_tag);

	W("\tfor (size_t _i = 0; _i < m->"); W(fname); W("_count; _i++) {\n");
	W("\t\tsize_t _inner = 0;\n");
	{
		char kref[128], vref[128];
		snprintf(kref, sizeof kref, "m->%s[_i].key", fname);
		snprintf(vref, sizeof vref, "m->%s[_i].value", fname);
		if ((err = emit_slot_size_contrib(w, 1, key, "_inner", kref)) != GREMLINP_OK) return err;
		if ((err = emit_slot_size_contrib(w, 2, val, "_inner", vref)) != GREMLINP_OK) return err;
	}
	W("\t\ts += "); WI((int32_t)outer_tag_bytes);
	W(" + gremlin_varint_size(_inner) + _inner;\n");
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_map_emit_encode_field(struct gremlinc_writer *w,
			       const struct gremlind_field *f,
			       const char *fname,
			       const struct map_slot_info *key,
			       const struct map_slot_info *val)
{
	enum gremlinp_parsing_error err;
	uint32_t outer_tag = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;

	W("\tfor (size_t _i = 0; _i < m->"); W(fname); W("_count; _i++) {\n");
	W("\t\tsize_t _inner = 0;\n");
	{
		char kref[128], vref[128];
		snprintf(kref, sizeof kref, "m->%s[_i].key", fname);
		snprintf(vref, sizeof vref, "m->%s[_i].value", fname);
		if ((err = emit_slot_size_contrib(w, 1, key, "_inner", kref)) != GREMLINP_OK) return err;
		if ((err = emit_slot_size_contrib(w, 2, val, "_inner", vref)) != GREMLINP_OK) return err;

		W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); WI((int32_t)outer_tag); W("u);\n");
		W("\t\t_off = gremlin_varint_encode_at(_buf, _off, _inner);\n");
		if ((err = emit_slot_encode(w, 1, key, kref)) != GREMLINP_OK) return err;
		if ((err = emit_slot_encode(w, 2, val, vref)) != GREMLINP_OK) return err;
	}
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_map_emit_reader_arm(struct gremlinc_writer *w,
			     const struct gremlind_field *f,
			     const char *fname)
{
	/* Identical to repeated: record first_offset on first hit, bump
	 * count, skip this element's length-delimited payload. */
	enum gremlinp_parsing_error err;
	uint32_t packed_tag = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;

	W("\t\tif (t.value == "); WI(packed_tag);
	W("u /* field "); WI(f->parsed.index); W(", GREMLIN_WIRE_LEN_PREFIX, map */) {\n");
	W("\t\t\tif (r->"); W(fname); W("_count == 0) {\n");
	W("\t\t\t\tr->"); W(fname); W("_first_offset = offset;\n");
	W("\t\t\t}\n");
	W("\t\t\tr->"); W(fname); W("_count++;\n");
	W("\t\t\tr->_has."); W(fname); W(" = 1;\n");
	W("\t\t\tstruct gremlin_bytes_decode_result d =\n"
	  "\t\t\t\tgremlin_bytes_decode(src + offset, len - offset);\n"
	  "\t\t\tif (d.error != GREMLIN_OK) return d.error;\n"
	  "\t\t\toffset += d.consumed;\n"
	  "\t\t\tcontinue;\n\t\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_map_emit_getter(struct gremlinc_writer *w,
			 struct gremlind_arena *arena,
			 const struct gremlind_field *f,
			 const char *reader_ty,
			 const char *fname,
			 const struct map_slot_info *key,
			 const struct map_slot_info *val)
{
	enum gremlinp_parsing_error err;
	uint32_t outer_packed = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;
	uint32_t key_tag = slot_inner_tag(1, key);
	uint32_t val_tag = slot_inner_tag(2, val);

	/* iter typedef */
	W("typedef struct "); W(reader_ty); W("_"); W(fname); W("_iter {\n");
	W("\tconst uint8_t\t*src;\n");
	W("\tsize_t\t\t src_len;\n");
	W("\tsize_t\t\t offset;\n");
	W("\tsize_t\t\t count_remaining;\n");
	W("} "); W(reader_ty); W("_"); W(fname); W("_iter;\n\n");

	/* count */
	W("static inline size_t\n");
	W(reader_ty); W("_"); W(fname); W("_count(const "); W(reader_ty); W(" *r)\n{\n");
	W("\tif (r == NULL) return 0;\n");
	W("\treturn r->"); W(fname); W("_count;\n}\n\n");

	/* begin */
	W("static inline "); W(reader_ty); W("_"); W(fname); W("_iter\n");
	W(reader_ty); W("_"); W(fname); W("_begin(const "); W(reader_ty); W(" *r)\n{\n");
	W("\t"); W(reader_ty); W("_"); W(fname); W("_iter it = {0};\n");
	W("\tif (r == NULL || r->"); W(fname); W("_count == 0) return it;\n");
	W("\tit.src = r->src;\n");
	W("\tit.src_len = r->src_len;\n");
	W("\tit.offset = r->"); W(fname); W("_first_offset;\n");
	W("\tit.count_remaining = r->"); W(fname); W("_count;\n");
	W("\treturn it;\n}\n\n");

	/* next(it, out_key, out_value) */
	const char *ktype = slot_out_param_type(arena, key);
	const char *vtype = slot_out_param_type(arena, val);
	if (ktype == NULL || vtype == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;

	W("static inline enum gremlin_error\n");
	W(reader_ty); W("_"); W(fname); W("_next(");
	W(reader_ty); W("_"); W(fname); W("_iter *it, ");
	W(ktype); W(" *out_key, "); W(vtype); W(" *out_value)\n{\n");
	/* Type defaults when absent */
	if (key->kind == GREMLINC_MS_MESSAGE) {
		/* unused — proto disallows message keys */
	} else {
		W("\t*out_key = "); W(slot_default_literal(key)); W(";\n");
	}
	if (val->kind == GREMLINC_MS_MESSAGE) {
		const char *tn = val->msg_ref->c_name;
		W("\tenum gremlin_error _ve0 = "); W(tn); W("_reader_init(out_value, NULL, 0);\n");
		W("\tif (_ve0 != GREMLIN_OK) return _ve0;\n");
	} else {
		W("\t*out_value = "); W(slot_default_literal(val)); W(";\n");
	}
	W("\tif (it->count_remaining == 0) return GREMLIN_OK;\n");
	/* Decode the outer entry's bytes into (ebuf, elen). */
	W("\tstruct gremlin_bytes_decode_result _entry =\n"
	  "\t\tgremlin_bytes_decode(it->src + it->offset, it->src_len - it->offset);\n"
	  "\tif (_entry.error != GREMLIN_OK) return _entry.error;\n"
	  "\tit->offset += _entry.consumed;\n"
	  "\tit->count_remaining--;\n"
	  "\tconst uint8_t *ebuf = _entry.bytes.data;\n"
	  "\tsize_t elen = _entry.bytes.len;\n"
	  "\tsize_t eoff = 0;\n");
	/* Walk the entry's inner tags: field 1 = key, field 2 = value,
	 * skip everything else (forward-compat). */
	W("\twhile (eoff < elen) {\n");
	W("\t\tstruct gremlin_varint32_decode_result t =\n"
	  "\t\t\tgremlin_varint32_decode(ebuf + eoff, elen - eoff);\n"
	  "\t\tif (t.error != GREMLIN_OK) return t.error;\n"
	  "\t\teoff += t.consumed;\n");
	W("\t\tif (t.value == "); WI((int32_t)key_tag); W("u) {\n");
	{
		const char *kout = (key->kind == GREMLINC_MS_MESSAGE) ? "out_key" : "(*out_key)";
		if ((err = emit_slot_decode(w, key, kout)) != GREMLINP_OK) return err;
	}
	W("\t\t\tcontinue;\n\t\t}\n");
	W("\t\tif (t.value == "); WI((int32_t)val_tag); W("u) {\n");
	{
		const char *vout = (val->kind == GREMLINC_MS_MESSAGE) ? "out_value" : "(*out_value)";
		if ((err = emit_slot_decode(w, val, vout)) != GREMLINP_OK) return err;
	}
	W("\t\t\tcontinue;\n\t\t}\n");
	W("\t\tunsigned _wt = (unsigned)(t.value % 8u);\n"
	  "\t\tif (_wt == 6u || _wt == 7u) return GREMLIN_ERROR_INVALID_WIRE_TYPE;\n"
	  "\t\tstruct gremlin_skip_result sk =\n"
	  "\t\t\tgremlin_skip_data(ebuf + eoff, elen - eoff, (enum gremlin_wire_type)_wt);\n"
	  "\t\tif (sk.error != GREMLIN_OK) return sk.error;\n"
	  "\t\teoff += sk.consumed;\n");
	W("\t}\n");
	/* Scan forward in outer buffer to next matching tag. */
	W("\twhile (it->count_remaining > 0 && it->offset < it->src_len) {\n"
	  "\t\tstruct gremlin_varint32_decode_result nt =\n"
	  "\t\t\tgremlin_varint32_decode(it->src + it->offset, it->src_len - it->offset);\n"
	  "\t\tif (nt.error != GREMLIN_OK) return nt.error;\n"
	  "\t\tit->offset += nt.consumed;\n");
	W("\t\tif (nt.value == "); WI((int32_t)outer_packed); W("u) break;\n");
	W("\t\tunsigned _wt2 = (unsigned)(nt.value % 8u);\n"
	  "\t\tif (_wt2 == 6u || _wt2 == 7u) return GREMLIN_ERROR_INVALID_WIRE_TYPE;\n"
	  "\t\tstruct gremlin_skip_result sk2 =\n"
	  "\t\t\tgremlin_skip_data(it->src + it->offset, it->src_len - it->offset,\n"
	  "\t\t\t                  (enum gremlin_wire_type)_wt2);\n"
	  "\t\tif (sk2.error != GREMLIN_OK) return sk2.error;\n"
	  "\t\tit->offset += sk2.consumed;\n\t}\n");
	W("\treturn GREMLIN_OK;\n}\n\n");
	(void)outer_packed;
	return GREMLINP_OK;
}

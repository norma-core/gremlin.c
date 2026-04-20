#include <stdint.h>
#include <stdio.h>

#include "gremlin.h"

#include "emit_common.h"

/*
 * `repeated <scalar>` / `repeated <enum>` emission — packed + unpacked
 * wire forms.
 *
 * Wire shapes supported:
 *   - Packed:   single tag (wire=LEN_PREFIX) + length varint + concat
 *               of element payloads.  Reader expects ALL occurrences
 *               to be packed if the first was; mixed forms are
 *               rejected with INVALID_WIRE_TYPE.
 *   - Unpacked: one tag + value triple per element.  Reader expects
 *               ALL occurrences to be unpacked if the first was.
 *
 * Writer heuristic: emit packed when count >= 2 (saves per-element
 * tag byte); emit unpacked when count == 1 (saves the length-prefix
 * byte).  Skip entirely when count == 0.
 *
 * Reader caches: count, first_tag_offset (points AT the first tag
 * byte, before consumption), and a packed/unpacked flag.  The
 * iterator re-reads each tag during iteration — this is how we
 * tolerate interleaved non-matching fields.
 */

#define WT_VARINT_NUM     0u
#define WT_FIXED64_NUM    1u
#define WT_LEN_PREFIX_NUM 2u
#define WT_FIXED32_NUM    5u

/* ------------------------------------------------------------------------
 * Element classification — per-field view of the element type.  For
 * repeated scalars the `scalar_info` tells us kind + wire.  For
 * repeated enums the wire is VARINT and the C type comes from the
 * enum descriptor.
 * ------------------------------------------------------------------------ */

struct rs_elem {
	bool				 is_enum;
	const struct scalar_info	*scalar;	/* non-NULL if !is_enum */
	const struct gremlind_enum	*enumeration;	/* non-NULL if is_enum */
	const char			*c_type;	/* element type in writer array */
	unsigned			 wire;		/* VARINT / FIXED32 / FIXED64 */
	enum scalar_kind		 kind;		/* for varint sub-branching */
};

/* Caller has already filtered to repeated ENUM or repeated numeric
 * BUILTIN — repeated message and repeated string/bytes dispatch to
 * sibling modules, and proto-invalid element types are rejected by
 * the descriptor resolver. */
static void
rs_classify(const struct gremlind_field *f, struct rs_elem *out)
{
	if (f->type.kind == GREMLIND_TYPE_REF_ENUM) {
		out->is_enum = true;
		out->enumeration = f->type.u.enumeration;
		out->scalar = NULL;
		out->c_type = f->type.u.enumeration->c_name;
		out->wire = WT_VARINT_NUM;
		out->kind = SCALAR_INT32;	/* enum treated as int32 on wire */
		return;
	}
	const struct scalar_info *si = gremlinc_scalar_lookup(
		f->type.u.builtin.start, f->type.u.builtin.length);
	out->is_enum = false;
	out->scalar = si;
	out->enumeration = NULL;
	out->c_type = si->c_type;
	out->wire = si->wire_type;
	out->kind = si->kind;
}

/* Emit an expression that evaluates to the wire byte count of a single
 * element referenced by `ref`.  Pure expression — no statements. */
static enum gremlinp_parsing_error
emit_elem_wire_size_expr(struct gremlinc_writer *w,
			 const struct rs_elem *e, const char *ref)
{
	enum gremlinp_parsing_error err;
	if (e->is_enum) {
		W("gremlin_varint_size((uint64_t)(int64_t)"); W(ref); W(")");
		return GREMLINP_OK;
	}
	if (e->wire == WT_FIXED32_NUM) { W("4"); return GREMLINP_OK; }
	if (e->wire == WT_FIXED64_NUM) { W("8"); return GREMLINP_OK; }
	/* VARINT */
	switch (e->kind) {
	case SCALAR_INT32:
		W("gremlin_varint_size((uint64_t)(int64_t)"); W(ref); W(")"); break;
	case SCALAR_INT64:
		W("gremlin_varint_size((uint64_t)"); W(ref); W(")"); break;
	case SCALAR_UINT32:
		W("gremlin_varint32_size("); W(ref); W(")"); break;
	case SCALAR_UINT64:
		W("gremlin_varint_size("); W(ref); W(")"); break;
	case SCALAR_SINT32:
		W("gremlin_varint32_size(gremlin_zigzag32("); W(ref); W("))"); break;
	case SCALAR_SINT64:
		W("gremlin_varint_size(gremlin_zigzag64("); W(ref); W("))"); break;
	case SCALAR_BOOL:
		W("1"); break;
	default:
		W("1"); break;
	}
	return GREMLINP_OK;
}

/* Emit the encode statement for a single element (NO tag) — raw value
 * write into the writer. */
static enum gremlinp_parsing_error
emit_elem_encode(struct gremlinc_writer *w,
		 const struct rs_elem *e, const char *ref)
{
	enum gremlinp_parsing_error err;
	if (e->is_enum) {
		W("\t\t_off = gremlin_varint_encode_at(_buf, _off, (uint64_t)(int64_t)"); W(ref); W(");\n");
		return GREMLINP_OK;
	}
	switch (e->wire) {
	case WT_FIXED32_NUM:
		switch (e->kind) {
		case SCALAR_FIXED32:
			W("\t\t_off = gremlin_fixed32_encode_at(_buf, _off, "); W(ref); W(");\n"); break;
		case SCALAR_SFIXED32:
			W("\t\t_off = gremlin_fixed32_encode_at(_buf, _off, (uint32_t)"); W(ref); W(");\n"); break;
		case SCALAR_FLOAT:
			W("\t\t_off = gremlin_fixed32_encode_at(_buf, _off, gremlin_f32_bits("); W(ref); W("));\n"); break;
		default: break;
		}
		return GREMLINP_OK;
	case WT_FIXED64_NUM:
		switch (e->kind) {
		case SCALAR_FIXED64:
			W("\t\t_off = gremlin_fixed64_encode_at(_buf, _off, "); W(ref); W(");\n"); break;
		case SCALAR_SFIXED64:
			W("\t\t_off = gremlin_fixed64_encode_at(_buf, _off, (uint64_t)"); W(ref); W(");\n"); break;
		case SCALAR_DOUBLE:
			W("\t\t_off = gremlin_fixed64_encode_at(_buf, _off, gremlin_f64_bits("); W(ref); W("));\n"); break;
		default: break;
		}
		return GREMLINP_OK;
	}
	/* VARINT */
	switch (e->kind) {
	case SCALAR_INT32:
		W("\t\t_off = gremlin_varint_encode_at(_buf, _off, (uint64_t)(int64_t)"); W(ref); W(");\n"); break;
	case SCALAR_INT64:
		W("\t\t_off = gremlin_varint_encode_at(_buf, _off, (uint64_t)"); W(ref); W(");\n"); break;
	case SCALAR_UINT32:
		W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); W(ref); W(");\n"); break;
	case SCALAR_UINT64:
		W("\t\t_off = gremlin_varint_encode_at(_buf, _off, "); W(ref); W(");\n"); break;
	case SCALAR_SINT32:
		W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, gremlin_zigzag32("); W(ref); W("));\n"); break;
	case SCALAR_SINT64:
		W("\t\t_off = gremlin_varint_encode_at(_buf, _off, gremlin_zigzag64("); W(ref); W("));\n"); break;
	case SCALAR_BOOL:
		W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); W(ref); W(" ? 1u : 0u);\n"); break;
	default: break;
	}
	return GREMLINP_OK;
}

/* Emit the decode statement for a single element (NO tag assumed —
 * caller already consumed any outer tag).  Reads from `ebuf + eoff`
 * over a `elen` limit, writes into `*out`, advances `eoff`. */
static enum gremlinp_parsing_error
emit_elem_decode(struct gremlinc_writer *w, const struct rs_elem *e)
{
	enum gremlinp_parsing_error err;
	if (e->is_enum) {
		W("\t\tstruct gremlin_varint_decode_result _d =\n"
		  "\t\t\tgremlin_varint_decode(it->src + it->offset, it->src_len - it->offset);\n"
		  "\t\tif (_d.error != GREMLIN_OK) return _d.error;\n"
		  "\t\tit->offset += _d.consumed;\n"
		  "\t\t*out = ("); W(e->enumeration->c_name);
		W(")(int32_t)(uint32_t)_d.value;\n");
		return GREMLINP_OK;
	}
	switch (e->wire) {
	case WT_FIXED32_NUM:
		W("\t\tstruct gremlin_fixed32_decode_result _d =\n"
		  "\t\t\tgremlin_fixed32_decode(it->src + it->offset, it->src_len - it->offset);\n"
		  "\t\tif (_d.error != GREMLIN_OK) return _d.error;\n"
		  "\t\tit->offset += 4;\n");
		switch (e->kind) {
		case SCALAR_FIXED32:  W("\t\t*out = _d.value;\n"); break;
		case SCALAR_SFIXED32: W("\t\t*out = (int32_t)_d.value;\n"); break;
		case SCALAR_FLOAT:    W("\t\t*out = gremlin_f32_from_bits(_d.value);\n"); break;
		default: break;
		}
		return GREMLINP_OK;
	case WT_FIXED64_NUM:
		W("\t\tstruct gremlin_fixed64_decode_result _d =\n"
		  "\t\t\tgremlin_fixed64_decode(it->src + it->offset, it->src_len - it->offset);\n"
		  "\t\tif (_d.error != GREMLIN_OK) return _d.error;\n"
		  "\t\tit->offset += 8;\n");
		switch (e->kind) {
		case SCALAR_FIXED64:  W("\t\t*out = _d.value;\n"); break;
		case SCALAR_SFIXED64: W("\t\t*out = (int64_t)_d.value;\n"); break;
		case SCALAR_DOUBLE:   W("\t\t*out = gremlin_f64_from_bits(_d.value);\n"); break;
		default: break;
		}
		return GREMLINP_OK;
	}
	/* VARINT */
	W("\t\tstruct gremlin_varint_decode_result _d =\n"
	  "\t\t\tgremlin_varint_decode(it->src + it->offset, it->src_len - it->offset);\n"
	  "\tif (_d.error != GREMLIN_OK) return _d.error;\n"
	  "\tit->offset += _d.consumed;\n");
	switch (e->kind) {
	case SCALAR_INT32:  W("\t\t*out = (int32_t)(uint32_t)_d.value;\n"); break;
	case SCALAR_INT64:  W("\t\t*out = (int64_t)_d.value;\n"); break;
	case SCALAR_UINT32: W("\t\t*out = (uint32_t)_d.value;\n"); break;
	case SCALAR_UINT64: W("\t\t*out = _d.value;\n"); break;
	case SCALAR_SINT32: W("\t\t*out = gremlin_unzigzag32((uint32_t)_d.value);\n"); break;
	case SCALAR_SINT64: W("\t\t*out = gremlin_unzigzag64(_d.value);\n"); break;
	case SCALAR_BOOL:   W("\t\t*out = (_d.value != 0);\n"); break;
	default: break;
	}
	return GREMLINP_OK;
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

enum gremlinp_parsing_error
gremlinc_repeated_scalar_emit_size_field(struct gremlinc_writer *w,
					 const struct gremlind_field *f,
					 const char *fname)
{
	enum gremlinp_parsing_error err;
	struct rs_elem e;
	rs_classify(f, &e);

	uint32_t packed_tag = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;
	uint32_t unpacked_tag = ((uint32_t)f->parsed.index << 3) | e.wire;
	unsigned packed_tag_bytes   = (unsigned)gremlin_varint_size(packed_tag);
	unsigned unpacked_tag_bytes = (unsigned)gremlin_varint_size(unpacked_tag);

	W("\tif (m->"); W(fname); W("_count == 1) {\n");
	W("\t\ts += "); WI((int32_t)unpacked_tag_bytes); W(" + ");
	{
		char ref[128];
		snprintf(ref, sizeof ref, "m->%s[0]", fname);
		if ((err = emit_elem_wire_size_expr(w, &e, ref)) != GREMLINP_OK) return err;
	}
	W(";\n\t} else if (m->"); W(fname); W("_count > 1) {\n");
	/* Packed payload length */
	if (!e.is_enum && e.wire == WT_FIXED32_NUM) {
		W("\t\tsize_t _payload = m->"); W(fname); W("_count * 4;\n");
	} else if (!e.is_enum && e.wire == WT_FIXED64_NUM) {
		W("\t\tsize_t _payload = m->"); W(fname); W("_count * 8;\n");
	} else {
		W("\t\tsize_t _payload = 0;\n");
		W("\t\tfor (size_t _i = 0; _i < m->"); W(fname); W("_count; _i++) {\n");
		W("\t\t\t_payload += ");
		{
			char ref[128];
			snprintf(ref, sizeof ref, "m->%s[_i]", fname);
			if ((err = emit_elem_wire_size_expr(w, &e, ref)) != GREMLINP_OK) return err;
		}
		W(";\n\t\t}\n");
	}
	W("\t\ts += "); WI((int32_t)packed_tag_bytes);
	W(" + gremlin_varint_size(_payload) + _payload;\n");
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_repeated_scalar_emit_encode_field(struct gremlinc_writer *w,
					   const struct gremlind_field *f,
					   const char *fname)
{
	enum gremlinp_parsing_error err;
	struct rs_elem e;
	rs_classify(f, &e);

	uint32_t packed_tag = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;
	uint32_t unpacked_tag = ((uint32_t)f->parsed.index << 3) | e.wire;

	W("\tif (m->"); W(fname); W("_count == 1) {\n");
	W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); WI((int32_t)unpacked_tag); W("u);\n");
	{
		char ref[128];
		snprintf(ref, sizeof ref, "m->%s[0]", fname);
		if ((err = emit_elem_encode(w, &e, ref)) != GREMLINP_OK) return err;
	}
	W("\t} else if (m->"); W(fname); W("_count > 1) {\n");
	/* Packed: re-compute payload length, then emit tag + len + elements. */
	if (!e.is_enum && e.wire == WT_FIXED32_NUM) {
		W("\t\tsize_t _payload = m->"); W(fname); W("_count * 4;\n");
	} else if (!e.is_enum && e.wire == WT_FIXED64_NUM) {
		W("\t\tsize_t _payload = m->"); W(fname); W("_count * 8;\n");
	} else {
		W("\t\tsize_t _payload = 0;\n");
		W("\t\tfor (size_t _i = 0; _i < m->"); W(fname); W("_count; _i++) {\n");
		W("\t\t\t_payload += ");
		{
			char ref[128];
			snprintf(ref, sizeof ref, "m->%s[_i]", fname);
			if ((err = emit_elem_wire_size_expr(w, &e, ref)) != GREMLINP_OK) return err;
		}
		W(";\n\t\t}\n");
	}
	W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); WI((int32_t)packed_tag); W("u);\n");
	W("\t\t_off = gremlin_varint_encode_at(_buf, _off, _payload);\n");
	W("\t\tfor (size_t _i = 0; _i < m->"); W(fname); W("_count; _i++) {\n");
	{
		char ref[128];
		snprintf(ref, sizeof ref, "m->%s[_i]", fname);
		if ((err = emit_elem_encode(w, &e, ref)) != GREMLINP_OK) return err;
	}
	W("\t\t}\n");
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_repeated_scalar_emit_reader_arm(struct gremlinc_writer *w,
					 const struct gremlind_field *f,
					 const char *fname,
					 const char *packed_bool_fname)
{
	enum gremlinp_parsing_error err;
	struct rs_elem e;
	rs_classify(f, &e);

	uint32_t packed_tag = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;
	uint32_t unpacked_tag = ((uint32_t)f->parsed.index << 3) | e.wire;

	/* Packed arm: detect conflict, record first occurrence, count
	 * elements in the block, advance. */
	W("\t\tif (t.value == "); WI((int32_t)packed_tag);
	W("u /* field "); WI(f->parsed.index); W(", packed */) {\n");
	W("\t\t\tif (r->"); W(fname); W("_count > 0 && !r->");
	W(packed_bool_fname); W(") return GREMLIN_ERROR_INVALID_WIRE_TYPE;\n");
	W("\t\t\tstruct gremlin_bytes_decode_result _bd =\n"
	  "\t\t\t\tgremlin_bytes_decode(src + offset, len - offset);\n"
	  "\t\t\tif (_bd.error != GREMLIN_OK) return _bd.error;\n");
	/* Count elements in the block based on element kind. */
	if (!e.is_enum && e.wire == WT_FIXED32_NUM) {
		W("\t\t\tsize_t _bcnt = _bd.bytes.len / 4;\n");
	} else if (!e.is_enum && e.wire == WT_FIXED64_NUM) {
		W("\t\t\tsize_t _bcnt = _bd.bytes.len / 8;\n");
	} else {
		/* VARINT: count continuation-terminator bytes. */
		W("\t\t\tsize_t _bcnt = 0;\n");
		W("\t\t\tfor (size_t _bi = 0; _bi < _bd.bytes.len; _bi++) {\n"
		  "\t\t\t\tif ((_bd.bytes.data[_bi] & 0x80u) == 0) _bcnt++;\n"
		  "\t\t\t}\n");
	}
	W("\t\t\tif (r->"); W(fname); W("_count == 0) {\n");
	W("\t\t\t\tr->"); W(fname); W("_first_offset = offset - t.consumed;\n");
	W("\t\t\t\tr->"); W(packed_bool_fname); W(" = true;\n");
	W("\t\t\t}\n");
	W("\t\t\tr->"); W(fname); W("_count += _bcnt;\n");
	W("\t\t\tr->_has."); W(fname); W(" = 1;\n");
	W("\t\t\toffset += _bd.consumed;\n");
	W("\t\t\tcontinue;\n\t\t}\n");

	/* Unpacked arm: detect conflict, record first occurrence, decode
	 * single value to advance offset, bump count by 1. */
	W("\t\tif (t.value == "); WI((int32_t)unpacked_tag);
	W("u /* field "); WI(f->parsed.index); W(", unpacked */) {\n");
	W("\t\t\tif (r->"); W(fname); W("_count > 0 && r->");
	W(packed_bool_fname); W(") return GREMLIN_ERROR_INVALID_WIRE_TYPE;\n");
	/* Decode and discard the value — we just need to advance offset. */
	if (!e.is_enum && e.wire == WT_FIXED32_NUM) {
		W("\t\t\tif (len - offset < 4) return GREMLIN_ERROR_TRUNCATED;\n");
		W("\t\t\tif (r->"); W(fname); W("_count == 0) {\n");
		W("\t\t\t\tr->"); W(fname); W("_first_offset = offset - t.consumed;\n");
		W("\t\t\t}\n");
		W("\t\t\tr->"); W(fname); W("_count++;\n");
		W("\t\t\tr->_has."); W(fname); W(" = 1;\n");
		W("\t\t\toffset += 4;\n");
	} else if (!e.is_enum && e.wire == WT_FIXED64_NUM) {
		W("\t\t\tif (len - offset < 8) return GREMLIN_ERROR_TRUNCATED;\n");
		W("\t\t\tif (r->"); W(fname); W("_count == 0) {\n");
		W("\t\t\t\tr->"); W(fname); W("_first_offset = offset - t.consumed;\n");
		W("\t\t\t}\n");
		W("\t\t\tr->"); W(fname); W("_count++;\n");
		W("\t\t\tr->_has."); W(fname); W(" = 1;\n");
		W("\t\t\toffset += 8;\n");
	} else {
		W("\t\t\tstruct gremlin_varint_decode_result _vd =\n"
		  "\t\t\t\tgremlin_varint_decode(src + offset, len - offset);\n"
		  "\t\t\tif (_vd.error != GREMLIN_OK) return _vd.error;\n");
		W("\t\t\tif (r->"); W(fname); W("_count == 0) {\n");
		W("\t\t\t\tr->"); W(fname); W("_first_offset = offset - t.consumed;\n");
		W("\t\t\t}\n");
		W("\t\t\tr->"); W(fname); W("_count++;\n");
		W("\t\t\tr->_has."); W(fname); W(" = 1;\n");
		W("\t\t\toffset += _vd.consumed;\n");
	}
	W("\t\t\tcontinue;\n\t\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_repeated_scalar_emit_getter(struct gremlinc_writer *w,
				     const struct gremlind_field *f,
				     const char *reader_ty,
				     const char *fname,
				     const char *packed_bool_fname)
{
	enum gremlinp_parsing_error err;
	struct rs_elem e;
	rs_classify(f, &e);

	uint32_t packed_tag = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;
	uint32_t unpacked_tag = ((uint32_t)f->parsed.index << 3) | e.wire;

	/* iter typedef */
	W("typedef struct "); W(reader_ty); W("_"); W(fname); W("_iter {\n");
	W("\tconst uint8_t\t*src;\n");
	W("\tsize_t\t\t src_len;\n");
	W("\tsize_t\t\t offset;\n");
	W("\tsize_t\t\t block_end;\n");
	W("\tsize_t\t\t count_remaining;\n");
	W("\tbool\t\t is_packed;\n");
	W("} "); W(reader_ty); W("_"); W(fname); W("_iter;\n\n");

	/* count */
	W("static inline size_t\n");
	W(reader_ty); W("_"); W(fname); W("_count(const "); W(reader_ty);
	W(" *r)\n{\n");
	W("\tif (r == NULL) return 0;\n");
	W("\treturn r->"); W(fname); W("_count;\n}\n\n");

	/* begin */
	W("static inline "); W(reader_ty); W("_"); W(fname); W("_iter\n");
	W(reader_ty); W("_"); W(fname); W("_begin(const "); W(reader_ty);
	W(" *r)\n{\n");
	W("\t"); W(reader_ty); W("_"); W(fname); W("_iter it = {0};\n");
	W("\tif (r == NULL || r->"); W(fname); W("_count == 0) return it;\n");
	W("\tit.src = r->src;\n");
	W("\tit.src_len = r->src_len;\n");
	W("\tit.offset = r->"); W(fname); W("_first_offset;\n");
	W("\tit.count_remaining = r->"); W(fname); W("_count;\n");
	W("\tit.is_packed = r->"); W(packed_bool_fname); W(";\n");
	W("\treturn it;\n}\n\n");

	/* next */
	W("static inline enum gremlin_error\n");
	W(reader_ty); W("_"); W(fname); W("_next(");
	W(reader_ty); W("_"); W(fname); W("_iter *it, ");
	W(e.c_type); W(" *out)\n{\n");
	/* Set default on no-more. */
	if (e.is_enum) {
		W("\t*out = (") ; W(e.c_type); W(")0;\n");
	} else {
		W("\t*out = "); W(gremlinc_default_check_rhs(e.kind)); W(";\n");
	}
	W("\tif (it->count_remaining == 0) return GREMLIN_OK;\n");
	/* Packed branch */
	W("\tif (it->is_packed) {\n");
	W("\t\tif (it->block_end <= it->offset) {\n");
	/* Scan forward for next PACKED_TAG. */
	W("\t\t\twhile (it->offset < it->src_len) {\n"
	  "\t\t\t\tstruct gremlin_varint32_decode_result _t =\n"
	  "\t\t\t\t\tgremlin_varint32_decode(it->src + it->offset, it->src_len - it->offset);\n"
	  "\t\t\t\tif (_t.error != GREMLIN_OK) return _t.error;\n"
	  "\t\t\t\tit->offset += _t.consumed;\n");
	W("\t\t\t\tif (_t.value == "); WI((int32_t)packed_tag); W("u) {\n");
	W("\t\t\t\t\tstruct gremlin_varint_decode_result _l =\n"
	  "\t\t\t\t\t\tgremlin_varint_decode(it->src + it->offset, it->src_len - it->offset);\n"
	  "\t\t\t\t\tif (_l.error != GREMLIN_OK) return _l.error;\n"
	  "\t\t\t\t\tit->offset += _l.consumed;\n"
	  "\t\t\t\t\tit->block_end = it->offset + (size_t)_l.value;\n"
	  "\t\t\t\t\tbreak;\n\t\t\t\t}\n");
	W("\t\t\t\tunsigned _wt = (unsigned)(_t.value % 8u);\n"
	  "\t\t\t\tif (_wt == 6u || _wt == 7u) return GREMLIN_ERROR_INVALID_WIRE_TYPE;\n"
	  "\t\t\t\tstruct gremlin_skip_result _sk =\n"
	  "\t\t\t\t\tgremlin_skip_data(it->src + it->offset, it->src_len - it->offset,\n"
	  "\t\t\t\t\t                  (enum gremlin_wire_type)_wt);\n"
	  "\t\t\t\tif (_sk.error != GREMLIN_OK) return _sk.error;\n"
	  "\t\t\t\tit->offset += _sk.consumed;\n\t\t\t}\n");
	W("\t\t\tif (it->block_end <= it->offset) return GREMLIN_ERROR_TRUNCATED;\n");
	W("\t\t}\n");
	/* Decode next element within the current block. */
	W("\t\t{\n");
	if ((err = emit_elem_decode(w, &e)) != GREMLINP_OK) return err;
	W("\t\t}\n");
	W("\t\tit->count_remaining--;\n");
	W("\t\tif (it->offset >= it->block_end) it->block_end = 0;\n");
	W("\t\treturn GREMLIN_OK;\n\t}\n");
	/* Unpacked branch */
	W("\twhile (it->offset < it->src_len) {\n"
	  "\t\tstruct gremlin_varint32_decode_result _t =\n"
	  "\t\t\tgremlin_varint32_decode(it->src + it->offset, it->src_len - it->offset);\n"
	  "\t\tif (_t.error != GREMLIN_OK) return _t.error;\n"
	  "\t\tit->offset += _t.consumed;\n");
	W("\t\tif (_t.value == "); WI((int32_t)unpacked_tag); W("u) {\n");
	/* Decode a single element */
	W("\t\t\t{\n");
	if ((err = emit_elem_decode(w, &e)) != GREMLINP_OK) return err;
	W("\t\t\t}\n");
	W("\t\t\tit->count_remaining--;\n");
	W("\t\t\treturn GREMLIN_OK;\n\t\t}\n");
	W("\t\tunsigned _wt = (unsigned)(_t.value % 8u);\n"
	  "\t\tif (_wt == 6u || _wt == 7u) return GREMLIN_ERROR_INVALID_WIRE_TYPE;\n"
	  "\t\tstruct gremlin_skip_result _sk =\n"
	  "\t\t\tgremlin_skip_data(it->src + it->offset, it->src_len - it->offset,\n"
	  "\t\t\t                  (enum gremlin_wire_type)_wt);\n"
	  "\t\tif (_sk.error != GREMLIN_OK) return _sk.error;\n"
	  "\t\tit->offset += _sk.consumed;\n\t}\n");
	W("\treturn GREMLIN_ERROR_TRUNCATED;\n}\n\n");
	return GREMLINP_OK;
}

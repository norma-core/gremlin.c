#include <stdint.h>

#include "gremlin.h"

#include "emit_common.h"

/*
 * `repeated string` / `repeated bytes` emission.
 *
 * Split from a generic "repeated" family because each element kind
 * (bytes/string vs messages vs packed scalars) has distinct wire and
 * C shapes.  This file owns the two length-delimited primitives; a
 * sibling `emit_repeated_msg.c` will land later for repeated messages,
 * and packed scalars need a different wire loop entirely.
 *
 * Writer: `struct gremlin_bytes *<fname>; size_t <fname>_count;` —
 * caller owns the array, count is authoritative (pointer may be NULL
 * when count == 0).  Zero-length elements serialize as "tag + len 0
 * + no payload" without special casing — `gremlin_write_bytes` with
 * `src_len == 0` is a no-op.
 *
 * Reader: caches only `<fname>_count` + `<fname>_first_offset`; no
 * array materialised.  Per-field iterator (`<reader_ty>_<fname>_iter`)
 * starts at first_offset, decodes one element per `next()`, and
 * scans forward skipping non-matching tags to reach the next
 * occurrence — matches gremlin.zig's lazy-iterator pattern.
 */

#define WT_LEN_PREFIX_NUM 2u

/* ------------------------------------------------------------------------ */

enum gremlinp_parsing_error
gremlinc_repeated_bytes_emit_size_field(struct gremlinc_writer *w,
					const struct gremlind_field *f,
					const char *fname)
{
	enum gremlinp_parsing_error err;
	uint32_t packed = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;
	unsigned tag_bytes = (unsigned)gremlin_varint_size(packed);

	W("\tfor (size_t _i = 0; _i < m->"); W(fname); W("_count; _i++) {\n");
	W("\t\ts += "); WI((int32_t)tag_bytes);
	W("\n\t\t   + gremlin_varint_size(m->"); W(fname); W("[_i].len)");
	W("\n\t\t   + m->"); W(fname); W("[_i].len;\n");
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_repeated_bytes_emit_encode_field(struct gremlinc_writer *w,
					  const struct gremlind_field *f,
					  const char *fname)
{
	enum gremlinp_parsing_error err;
	uint32_t packed = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;

	W("\tfor (size_t _i = 0; _i < m->"); W(fname); W("_count; _i++) {\n");
	W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); WI((int32_t)packed); W("u);\n");
	W("\t\t_off = gremlin_varint_encode_at(_buf, _off, m->"); W(fname); W("[_i].len);\n");
	W("\t\t_off = gremlin_write_bytes_at(_buf, _off, m->"); W(fname); W("[_i].data, m->");
	W(fname); W("[_i].len);\n");
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_repeated_bytes_emit_reader_arm(struct gremlinc_writer *w,
					const struct gremlind_field *f,
					const char *fname)
{
	/* Record first_offset on first hit; bump count; skip payload. */
	enum gremlinp_parsing_error err;
	uint32_t packed_tag = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;

	W("\t\tif (t.value == "); WI(packed_tag);
	W("u /* field "); WI(f->parsed.index); W(", GREMLIN_WIRE_LEN_PREFIX, repeated */) {\n");
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

/* ------------------------------------------------------------------------
 * Iterator + next — emitted in place of the scalar "return r->x" getter. */

enum gremlinp_parsing_error
gremlinc_repeated_bytes_emit_getter(struct gremlinc_writer *w,
				    const struct gremlind_field *f,
				    const char *reader_ty,
				    const char *fname)
{
	enum gremlinp_parsing_error err;
	uint32_t packed = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;

	/* typedef <reader_ty>_<fname>_iter */
	W("typedef struct "); W(reader_ty); W("_"); W(fname); W("_iter {\n");
	W("\tconst uint8_t\t*src;\n");
	W("\tsize_t\t\t src_len;\n");
	W("\tsize_t\t\t offset;\n");
	W("\tsize_t\t\t count_remaining;\n");
	W("} "); W(reader_ty); W("_"); W(fname); W("_iter;\n\n");

	/* <reader_ty>_<fname>_count(r) */
	W("static inline size_t\n");
	W(reader_ty); W("_"); W(fname); W("_count(const "); W(reader_ty);
	W(" *r)\n{\n");
	W("\tif (r == NULL) return 0;\n");
	W("\treturn r->"); W(fname); W("_count;\n}\n\n");

	/* <reader_ty>_<fname>_begin(r) */
	W("static inline "); W(reader_ty); W("_"); W(fname); W("_iter\n");
	W(reader_ty); W("_"); W(fname); W("_begin(const "); W(reader_ty);
	W(" *r)\n{\n");
	W("\t"); W(reader_ty); W("_"); W(fname); W("_iter it = {0};\n");
	W("\tif (r == NULL || r->"); W(fname); W("_count == 0) return it;\n");
	W("\tit.src = r->src;\n");
	W("\tit.src_len = r->src_len;\n");
	W("\tit.offset = r->"); W(fname); W("_first_offset;\n");
	W("\tit.count_remaining = r->"); W(fname); W("_count;\n");
	W("\treturn it;\n}\n\n");

	/* <reader_ty>_<fname>_next(it, out) */
	W("static inline enum gremlin_error\n");
	W(reader_ty); W("_"); W(fname); W("_next(");
	W(reader_ty); W("_"); W(fname); W("_iter *it, struct gremlin_bytes *out)\n{\n");
	W("\tif (it->count_remaining == 0) {\n"
	  "\t\tout->data = NULL; out->len = 0;\n"
	  "\t\treturn GREMLIN_OK;\n\t}\n");
	W("\tstruct gremlin_bytes_decode_result d =\n"
	  "\t\tgremlin_bytes_decode(it->src + it->offset, it->src_len - it->offset);\n"
	  "\tif (d.error != GREMLIN_OK) return d.error;\n"
	  "\t*out = d.bytes;\n"
	  "\tit->offset += d.consumed;\n"
	  "\tit->count_remaining--;\n");
	/* Scan forward to next matching tag if more elements remain. */
	W("\twhile (it->count_remaining > 0 && it->offset < it->src_len) {\n");
	W("\t\tstruct gremlin_varint32_decode_result t =\n"
	  "\t\t\tgremlin_varint32_decode(it->src + it->offset, it->src_len - it->offset);\n"
	  "\t\tif (t.error != GREMLIN_OK) return t.error;\n"
	  "\t\tit->offset += t.consumed;\n");
	W("\t\tif (t.value == "); WI((int32_t)packed); W("u) break;\n");
	W("\t\tunsigned _wt = (unsigned)(t.value % 8u);\n"
	  "\t\tif (_wt == 6u || _wt == 7u) return GREMLIN_ERROR_INVALID_WIRE_TYPE;\n"
	  "\t\tstruct gremlin_skip_result sk =\n"
	  "\t\t\tgremlin_skip_data(it->src + it->offset, it->src_len - it->offset,\n"
	  "\t\t\t                  (enum gremlin_wire_type)_wt);\n"
	  "\t\tif (sk.error != GREMLIN_OK) return sk.error;\n"
	  "\t\tit->offset += sk.consumed;\n\t}\n");
	W("\treturn GREMLIN_OK;\n}\n\n");
	return GREMLINP_OK;
}

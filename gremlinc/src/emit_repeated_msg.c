#include <stdint.h>

#include "gremlin.h"

#include "emit_common.h"

/*
 * `repeated <message>` emission — sibling of emit_repeated_bytes.c.
 *
 * Same wire shape (each element is LEN_PREFIX), same reader state
 * (count + first_offset, lazy iterator), but different element handling:
 *
 *   Writer: `const <Target> * const *<fname>; size_t <fname>_count;`
 *           Each slot is a pointer to the caller-owned child message;
 *           slots MAY be NULL and serialize as "tag + length-0" (empty
 *           child).  No special-casing required for either size or
 *           encode — a NULL slot just produces an empty-body length.
 *
 *   Reader: same count + first_offset cache as repeated bytes.  The
 *           iterator's `next()` takes a caller-provided
 *           `<Target>_reader *out` and runs `<Target>_reader_init`
 *           over the current element's byte slice, matching the
 *           non-repeated msg-ref getter convention.
 */

#define WT_LEN_PREFIX_NUM 2u

enum gremlinp_parsing_error
gremlinc_repeated_msg_emit_size_field(struct gremlinc_writer *w,
				      const struct gremlind_field *f,
				      const char *fname)
{
	enum gremlinp_parsing_error err;
	const char *tname = f->type.u.message->c_name;

	uint32_t packed = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;
	unsigned tag_bytes = (unsigned)gremlin_varint_size(packed);

	/* NULL slot → child size 0 (empty embedded message on wire).
	 * Mirrors the single-branch form used by the encode loop — one
	 * NULL check per element instead of a ternary. */
	W("\tfor (size_t _i = 0; _i < m->"); W(fname); W("_count; _i++) {\n");
	W("\t\tconst "); W(tname); W(" *_el = m->"); W(fname); W("[_i];\n");
	W("\t\tif (_el != NULL) {\n");
	W("\t\t\tsize_t _child_size = "); W(tname); W("_size(_el);\n");
	W("\t\t\ts += "); WI((int32_t)tag_bytes);
	W("\n\t\t\t   + gremlin_varint_size(_child_size)");
	W("\n\t\t\t   + _child_size;\n");
	W("\t\t} else {\n");
	W("\t\t\ts += "); WI((int32_t)tag_bytes); W(" + 1;\n");
	W("\t\t}\n");
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_repeated_msg_emit_encode_field(struct gremlinc_writer *w,
					const struct gremlind_field *f,
					const char *fname)
{
	enum gremlinp_parsing_error err;
	const char *tname = f->type.u.message->c_name;

	uint32_t packed = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;

	/* Two divergent paths under one NULL check: non-null branch does
	 * size+tag+len+encode; NULL branch writes just tag+zero-length.
	 * Previous form issued the NULL check twice (once in a ternary for
	 * the size, once again before encode); the writer call in between
	 * could alias through `w->buf`, so the compiler couldn't merge them
	 * — cost one extra unpredictable branch per array element, which
	 * compounded at deep nesting. Structure now mirrors zig's encode
	 * loop. */
	/* Length prefix via `<tname>_cached_size(_el)` — forward-declared
	 * inline getter for `_el->_size`, so this works even when the
	 * target message's struct hasn't been fully defined yet. */
	W("\tfor (size_t _i = 0; _i < m->"); W(fname); W("_count; _i++) {\n");
	W("\t\tconst "); W(tname); W(" *_el = m->"); W(fname); W("[_i];\n");
	W("\t\tif (_el != NULL) {\n");
	W("\t\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); WI((int32_t)packed); W("u);\n");
	W("\t\t\t_off = gremlin_varint_encode_at(_buf, _off, "); W(tname); W("_cached_size(_el));\n");
	W("\t\t\t_off = "); W(tname); W("_encode_at(_el, _buf, _off);\n");
	W("\t\t} else {\n");
	W("\t\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); WI((int32_t)packed); W("u);\n");
	W("\t\t\t_off = gremlin_varint_encode_at(_buf, _off, 0);\n");
	W("\t\t}\n");
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_repeated_msg_emit_reader_arm(struct gremlinc_writer *w,
				      const struct gremlind_field *f,
				      const char *fname)
{
	/* Identical to repeated_bytes's reader arm — wire is the same
	 * (LEN_PREFIX), we just track count + first_offset + skip. */
	enum gremlinp_parsing_error err;
	uint32_t packed_tag = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;

	W("\t\tif (t.value == "); WI(packed_tag);
	W("u /* field "); WI(f->parsed.index);
	W(", GREMLIN_WIRE_LEN_PREFIX, repeated msg */) {\n");
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

/* Iterator + begin + next.  Identical to repeated_bytes except `next`
 * takes a `<Target>_reader *out` and runs the target's `_reader_init`
 * on the current element's byte slice. */
enum gremlinp_parsing_error
gremlinc_repeated_msg_emit_getter(struct gremlinc_writer *w,
				  const struct gremlind_field *f,
				  const char *reader_ty,
				  const char *fname)
{
	enum gremlinp_parsing_error err;
	uint32_t packed = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;
	const char *tname = f->type.u.message->c_name;

	/* iter typedef */
	W("typedef struct "); W(reader_ty); W("_"); W(fname); W("_iter {\n");
	W("\tconst uint8_t\t*src;\n");
	W("\tsize_t\t\t src_len;\n");
	W("\tsize_t\t\t offset;\n");
	W("\tsize_t\t\t count_remaining;\n");
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
	W("\treturn it;\n}\n\n");

	/* next: decode current bytes, init child reader, advance */
	W("static inline enum gremlin_error\n");
	W(reader_ty); W("_"); W(fname); W("_next(");
	W(reader_ty); W("_"); W(fname); W("_iter *it, "); W(tname);
	W("_reader *out)\n{\n");
	W("\tif (it->count_remaining == 0) {\n"
	  "\t\treturn "); W(tname); W("_reader_init(out, NULL, 0);\n\t}\n");
	W("\tstruct gremlin_bytes_decode_result d =\n"
	  "\t\tgremlin_bytes_decode(it->src + it->offset, it->src_len - it->offset);\n"
	  "\tif (d.error != GREMLIN_OK) return d.error;\n"
	  "\tit->offset += d.consumed;\n"
	  "\tit->count_remaining--;\n");
	W("\tenum gremlin_error _ie = "); W(tname);
	W("_reader_init(out, d.bytes.data, d.bytes.len);\n"
	  "\tif (_ie != GREMLIN_OK) return _ie;\n");
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

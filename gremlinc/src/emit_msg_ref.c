#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gremlin.h"

#include "emit_common.h"

/*
 * Message-reference fields — proto fields whose type is a named message.
 *
 * Wire shape: LEN_PREFIX (wire type 2), payload is the child message's
 * serialized bytes.  Encode computes the child's size via its generated
 * `Target_size` + writes tag, length-varint, then `Target_encode` into
 * the writer.
 *
 * C shape: pointer to the target's writer struct — `const Target *`.
 * NULL means "field not set" (proto3 default).  Caller owns the pointee
 * — gremlinc never allocates.
 *
 * Reader side: parent caches `(data, len)` per message field plus the
 * has-bit.  The getter is lazy: given a caller-provided child reader,
 * it runs `Target_reader_init` over the cached bytes.  No double-init
 * on first access; repeated gets re-init (caller caches if needed).
 */

#define WT_LEN_PREFIX_NUM 2u

/* ------------------------------------------------------------------------ */

enum gremlinp_parsing_error
gremlinc_msg_ref_emit_size_field(struct gremlinc_writer *w,
				 const struct gremlind_field *f,
				 const char *fname)
{
	enum gremlinp_parsing_error err;
	const char *tname = f->type.u.message->c_name;

	uint32_t packed = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;
	unsigned tag_bytes = (unsigned)gremlin_varint_size(packed);

	/* Compute child size once — emit uses it for both the length
	 * prefix AND the payload contribution. */
	W("\tif (m->"); W(fname); W(" != NULL) {\n");
	W("\t\tsize_t _child_size = "); W(tname); W("_size(m->"); W(fname); W(");\n");
	W("\t\ts += "); WI((int32_t)tag_bytes);
	W("\n\t\t   + gremlin_varint_size(_child_size)");
	W("\n\t\t   + _child_size;\n");
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_msg_ref_emit_encode_field(struct gremlinc_writer *w,
				   const struct gremlind_field *f,
				   const char *fname)
{
	enum gremlinp_parsing_error err;
	const char *tname = f->type.u.message->c_name;

	uint32_t packed = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;

	/* Tag + length + nested body all via `_at` calls — offset stays
	 * in a register across the whole sub-tree, no writer round-trip.
	 * Length prefix goes through `<tname>_cached_size` (a forward-
	 * declared inline getter that reads `_size`), avoiding a direct
	 * field access that would require the child struct's full
	 * definition to be visible at this emission site. */
	W("\tif (m->"); W(fname); W(" != NULL) {\n");
	W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); WI((int32_t)packed); W("u);\n");
	W("\t\t_off = gremlin_varint_encode_at(_buf, _off, "); W(tname); W("_cached_size(m->"); W(fname); W("));\n");
	W("\t\t_off = "); W(tname); W("_encode_at(m->"); W(fname); W(", _buf, _off);\n");
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_msg_ref_emit_reader_arm(struct gremlinc_writer *w,
				 const struct gremlind_field *f,
				 const char *fname)
{
	enum gremlinp_parsing_error err;
	uint32_t packed_tag = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;

	/* Reader caches the child's byte slice as `struct gremlin_bytes`
	 * under the proto field name — same cache slot as a native bytes
	 * field.  The getter lazily inits a Target_reader from it. */
	W("\t\tif (t.value == "); WI(packed_tag);
	W("u /* field "); WI(f->parsed.index); W(", GREMLIN_WIRE_LEN_PREFIX */) {\n");
	W("\t\t\tstruct gremlin_bytes_decode_result d =\n"
	  "\t\t\t\tgremlin_bytes_decode(src + offset, len - offset);\n"
	  "\t\t\tif (d.error != GREMLIN_OK) return d.error;\n"
	  "\t\t\toffset += d.consumed;\n"
	  "\t\t\tr->"); W(fname); W(" = d.bytes;\n");
	W("\t\t\tr->_has."); W(fname); W(" = 1;\n");
	W("\t\t\tcontinue;\n\t\t}\n");
	return GREMLINP_OK;
}

/* Default is always `NULL` for message-ref fields — proto3 implicit
 * "not set" + proto spec disallows `[default]` on message types. */
const char *
gremlinc_msg_ref_default_literal(void)
{
	return "NULL";
}

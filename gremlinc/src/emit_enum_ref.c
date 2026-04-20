#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gremlin.h"
#include "gremlinc/const_convert.h"

#include "emit_common.h"

/*
 * Enum-reference fields — proto fields whose type is a named enum.
 *
 * Wire shape: VARINT (wire type 0), value is a signed int32 on the
 * wire.  Protobuf treats enum values like `int32` — negatives
 * sign-extend to 10 bytes, positives take 1–5 bytes.  Encode uses the
 * 64-bit varint primitive for that reason.
 *
 * C shape: the enum typedef (e.g. `google_protobuf_NullValue value`),
 * read from the target descriptor's `c_name` which was pre-assigned
 * by `gremlinc_assign_c_names`.
 *
 * Default: proto3 spec fixes the first enum value's number at 0, so
 * the implicit default is `0` — interpretable as that first value.
 * Proto2 `[default = <ident>]` is handled by `gremlinc_const_to_enum`,
 * which looks the identifier up against the target enum's declared
 * values and returns its integer. Numeric defaults
 * (`[default = 7]`) go through the same helper as int32 conversion.
 */

#define WT_VARINT_NUM 0u

enum gremlinp_parsing_error
gremlinc_enum_ref_emit_size_field(struct gremlinc_writer *w,
				  const struct gremlind_field *f,
				  const char *fname,
				  const char *default_lit)
{
	enum gremlinp_parsing_error err;

	uint32_t packed = ((uint32_t)f->parsed.index << 3) | WT_VARINT_NUM;
	unsigned tag_bytes = (unsigned)gremlin_varint_size(packed);

	W("\tif (m->"); W(fname); W(" != "); W(default_lit); W(") {\n");
	W("\t\ts += "); WI((int32_t)tag_bytes);
	/* int32 sign-extension path: treat enum as int32 on the wire. */
	W("\n\t\t   + gremlin_varint_size((uint64_t)(int64_t)m->");
	W(fname); W(");\n");
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_enum_ref_emit_encode_field(struct gremlinc_writer *w,
				    const struct gremlind_field *f,
				    const char *fname,
				    const char *default_lit)
{
	enum gremlinp_parsing_error err;

	uint32_t packed = ((uint32_t)f->parsed.index << 3) | WT_VARINT_NUM;

	W("\tif (m->"); W(fname); W(" != "); W(default_lit); W(") {\n");
	W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); WI((int32_t)packed); W("u);\n");
	W("\t\t_off = gremlin_varint_encode_at(_buf, _off, (uint64_t)(int64_t)m->"); W(fname); W(");\n");
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_enum_ref_emit_reader_arm(struct gremlinc_writer *w,
				  const struct gremlind_field *f,
				  const char *fname)
{
	enum gremlinp_parsing_error err;
	uint32_t packed_tag = ((uint32_t)f->parsed.index << 3) | WT_VARINT_NUM;

	W("\t\tif (t.value == "); WI(packed_tag);
	W("u /* field "); WI(f->parsed.index); W(", GREMLIN_WIRE_VARINT */) {\n");
	W("\t\t\tstruct gremlin_varint_decode_result d =\n"
	  "\t\t\t\tgremlin_varint_decode(src + offset, len - offset);\n"
	  "\t\t\tif (d.error != GREMLIN_OK) return d.error;\n"
	  "\t\t\toffset += d.consumed;\n");
	/* Truncate 64→32 bits, reinterpret as signed, cast to enum type. */
	W("\t\t\tr->"); W(fname); W(" = ("); W(f->type.u.enumeration->c_name);
	W(")(int32_t)(uint32_t)d.value;\n");
	W("\t\t\tr->_has."); W(fname); W(" = 1;\n");
	W("\t\t\tcontinue;\n\t\t}\n");
	return GREMLINP_OK;
}

const char *
gremlinc_enum_ref_resolve_default(struct gremlind_arena *arena,
				  const struct gremlind_field *f)
{
	if (!f->has_default) {
		/* Proto3: first enum value is always 0, so the implicit
		 * default is the zero constant — coerces to the enum type
		 * on assignment. */
		return "0";
	}

	/* Delegate to the validated const_convert layer — it handles both
	 * integer-literal form and identifier-name lookup against the
	 * target enum's declared values. */
	struct gremlinc_int32_convert_result r =
		gremlinc_const_to_enum(&f->default_value, f->type.u.enumeration);
	if (r.error != GREMLINP_OK) return NULL;
	char buf[32];
	int n = snprintf(buf, sizeof buf, "%d", r.value);
	if (n <= 0 || n >= (int)sizeof buf) return NULL;
	char *out = gremlind_arena_alloc(arena, (size_t)n + 1);
	if (out == NULL) return NULL;
	memcpy(out, buf, (size_t)n + 1);
	return out;
}

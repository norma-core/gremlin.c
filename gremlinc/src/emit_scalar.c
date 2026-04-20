#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gremlin.h"
#include "gremlinc/const_convert.h"

#include "emit_common.h"

/*
 * Scalar-numeric + bool emission:
 *   int32, int64, uint32, uint64, sint32, sint64,
 *   fixed32, fixed64, sfixed32, sfixed64, float, double, bool
 *
 * Wire shapes handled:
 *   VARINT  — int/uint/sint (10-byte for negative int*, 5-byte for uint/sint),
 *             bool
 *   FIXED32 — fixed32, sfixed32, float (4 bytes LE)
 *   FIXED64 — fixed64, sfixed64, double (8 bytes LE)
 */

/* ------------------------------------------------------------------------
 * Size
 * ------------------------------------------------------------------------ */

enum gremlinp_parsing_error
gremlinc_scalar_emit_size_field(struct gremlinc_writer *w,
				const struct gremlind_field *f,
				const struct scalar_info *si,
				const char *fname,
				const char *default_lit)
{
	enum gremlinp_parsing_error err;

	uint32_t packed = ((uint32_t)f->parsed.index << 3) | si->wire_type;
	unsigned tag_bytes = (unsigned)gremlin_varint_size(packed);

	W("\tif (m->"); W(fname); W(" != "); W(default_lit); W(") {\n");
	W("\t\ts += ");
	WI((int32_t)tag_bytes);		/* codegen-precomputed tag byte count */

	switch (si->kind) {
	case SCALAR_INT32:
		/* Sign-extend to int64 before widening to uint64 so negatives
		 * encode as 10-byte varints per protobuf spec. */
		W("\n\t\t   + gremlin_varint_size((uint64_t)(int64_t)m->");
		W(fname); W(")");
		break;
	case SCALAR_INT64:
		W("\n\t\t   + gremlin_varint_size((uint64_t)m->"); W(fname); W(")");
		break;
	case SCALAR_UINT32:
		W(" + gremlin_varint32_size(m->"); W(fname); W(")");
		break;
	case SCALAR_UINT64:
		W("\n\t\t   + gremlin_varint_size(m->"); W(fname); W(")");
		break;
	case SCALAR_SINT32:
		W(" + gremlin_varint32_size(gremlin_zigzag32(m->"); W(fname); W("))");
		break;
	case SCALAR_SINT64:
		W("\n\t\t   + gremlin_varint_size(gremlin_zigzag64(m->"); W(fname); W("))");
		break;
	case SCALAR_FIXED32:
	case SCALAR_SFIXED32:
	case SCALAR_FLOAT:
		W(" + 4");
		break;
	case SCALAR_FIXED64:
	case SCALAR_SFIXED64:
	case SCALAR_DOUBLE:
		W(" + 8");
		break;
	case SCALAR_BOOL:
		W(" + 1");
		break;
	}

	W(";\n\t}\n");
	return GREMLINP_OK;
}

/* ------------------------------------------------------------------------
 * Encode
 * ------------------------------------------------------------------------ */

enum gremlinp_parsing_error
gremlinc_scalar_emit_encode_field(struct gremlinc_writer *w,
				  const struct gremlind_field *f,
				  const struct scalar_info *si,
				  const char *fname,
				  const char *default_lit)
{
	enum gremlinp_parsing_error err;

	uint32_t packed = ((uint32_t)f->parsed.index << 3) | si->wire_type;

	W("\tif (m->"); W(fname); W(" != "); W(default_lit); W(") {\n");
	/* `_at` variants take (buf, off) by value and return new off —
	 * keeps offset in a register for the whole encode body (see
	 * gremlinc_emit_message's `_buf` / `_off` locals). */
	W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); WI((int32_t)packed); W("u);\n");

	switch (si->kind) {
	case SCALAR_INT32:
		W("\t\t_off = gremlin_varint_encode_at(_buf, _off, (uint64_t)(int64_t)m->"); W(fname); W(");\n");
		break;
	case SCALAR_INT64:
		W("\t\t_off = gremlin_varint_encode_at(_buf, _off, (uint64_t)m->"); W(fname); W(");\n");
		break;
	case SCALAR_UINT32:
		W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, m->"); W(fname); W(");\n");
		break;
	case SCALAR_UINT64:
		W("\t\t_off = gremlin_varint_encode_at(_buf, _off, m->"); W(fname); W(");\n");
		break;
	case SCALAR_SINT32:
		W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, gremlin_zigzag32(m->"); W(fname); W("));\n");
		break;
	case SCALAR_SINT64:
		W("\t\t_off = gremlin_varint_encode_at(_buf, _off, gremlin_zigzag64(m->"); W(fname); W("));\n");
		break;
	case SCALAR_FIXED32:
		W("\t\t_off = gremlin_fixed32_encode_at(_buf, _off, m->"); W(fname); W(");\n");
		break;
	case SCALAR_FIXED64:
		W("\t\t_off = gremlin_fixed64_encode_at(_buf, _off, m->"); W(fname); W(");\n");
		break;
	case SCALAR_SFIXED32:
		W("\t\t_off = gremlin_fixed32_encode_at(_buf, _off, (uint32_t)m->"); W(fname); W(");\n");
		break;
	case SCALAR_SFIXED64:
		W("\t\t_off = gremlin_fixed64_encode_at(_buf, _off, (uint64_t)m->"); W(fname); W(");\n");
		break;
	case SCALAR_DOUBLE:
		W("\t\t_off = gremlin_fixed64_encode_at(_buf, _off, gremlin_f64_bits(m->"); W(fname); W("));\n");
		break;
	case SCALAR_FLOAT:
		W("\t\t_off = gremlin_fixed32_encode_at(_buf, _off, gremlin_f32_bits(m->"); W(fname); W("));\n");
		break;
	case SCALAR_BOOL:
		W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, m->"); W(fname); W(" ? 1u : 0u);\n");
		break;
	}

	W("\t}\n");
	return GREMLINP_OK;
}

/* ------------------------------------------------------------------------
 * Reader arm — the `if (t.value == <packed>)` branch inside the init loop.
 * ------------------------------------------------------------------------ */

enum gremlinp_parsing_error
gremlinc_scalar_emit_reader_arm(struct gremlinc_writer *w,
				const struct gremlind_field *f,
				const struct scalar_info *si,
				const char *fname)
{
	enum gremlinp_parsing_error err;
	uint32_t packed_tag = ((uint32_t)f->parsed.index << 3) | si->wire_type;

	W("\t\tif (t.value == "); WI(packed_tag); W("u /* field ");
	WI(f->parsed.index); W(", "); W(si->wire_type_tok); W(" */) {\n");

	/* Dispatch on the scalar's wire family to pick the decode primitive. */
	bool is_varint  = (si->kind == SCALAR_INT32 || si->kind == SCALAR_INT64 ||
			   si->kind == SCALAR_UINT32 || si->kind == SCALAR_UINT64 ||
			   si->kind == SCALAR_SINT32 || si->kind == SCALAR_SINT64 ||
			   si->kind == SCALAR_BOOL);
	bool is_fixed32 = (si->kind == SCALAR_FIXED32 || si->kind == SCALAR_SFIXED32 ||
			   si->kind == SCALAR_FLOAT);

	if (is_varint) {
		if (gremlinc_kind_uses_varint32(si->kind)) {
			W("\t\t\tstruct gremlin_varint32_decode_result d =\n"
			  "\t\t\t\tgremlin_varint32_decode(src + offset, len - offset);\n"
			  "\t\t\tif (d.error != GREMLIN_OK) return d.error;\n"
			  "\t\t\toffset += d.consumed;\n");
			switch (si->kind) {
			case SCALAR_UINT32:
				W("\t\t\tr->"); W(fname); W(" = d.value;\n");
				break;
			case SCALAR_SINT32:
				W("\t\t\tr->"); W(fname); W(" = gremlin_unzigzag32(d.value);\n");
				break;
			case SCALAR_BOOL:
				W("\t\t\tr->"); W(fname); W(" = (d.value != 0);\n");
				break;
			default: break;
			}
		} else {
			W("\t\t\tstruct gremlin_varint_decode_result d =\n"
			  "\t\t\t\tgremlin_varint_decode(src + offset, len - offset);\n"
			  "\t\t\tif (d.error != GREMLIN_OK) return d.error;\n"
			  "\t\t\toffset += d.consumed;\n");
			switch (si->kind) {
			case SCALAR_INT32:
				W("\t\t\tr->"); W(fname); W(" = (int32_t)(uint32_t)d.value;\n");
				break;
			case SCALAR_INT64:
				W("\t\t\tr->"); W(fname); W(" = (int64_t)d.value;\n");
				break;
			case SCALAR_UINT64:
				W("\t\t\tr->"); W(fname); W(" = d.value;\n");
				break;
			case SCALAR_SINT64:
				W("\t\t\tr->"); W(fname); W(" = gremlin_unzigzag64(d.value);\n");
				break;
			default: break;
			}
		}
	} else if (is_fixed32) {
		W("\t\t\tstruct gremlin_fixed32_decode_result d =\n"
		  "\t\t\t\tgremlin_fixed32_decode(src + offset, len - offset);\n"
		  "\t\t\tif (d.error != GREMLIN_OK) return d.error;\n"
		  "\t\t\toffset += 4;\n");
		switch (si->kind) {
		case SCALAR_FIXED32:
			W("\t\t\tr->"); W(fname); W(" = d.value;\n");
			break;
		case SCALAR_SFIXED32:
			W("\t\t\tr->"); W(fname); W(" = (int32_t)d.value;\n");
			break;
		case SCALAR_FLOAT:
			W("\t\t\tr->"); W(fname); W(" = gremlin_f32_from_bits(d.value);\n");
			break;
		default: break;
		}
	} else {	/* FIXED64 */
		W("\t\t\tstruct gremlin_fixed64_decode_result d =\n"
		  "\t\t\t\tgremlin_fixed64_decode(src + offset, len - offset);\n"
		  "\t\t\tif (d.error != GREMLIN_OK) return d.error;\n"
		  "\t\t\toffset += 8;\n");
		switch (si->kind) {
		case SCALAR_FIXED64:
			W("\t\t\tr->"); W(fname); W(" = d.value;\n");
			break;
		case SCALAR_SFIXED64:
			W("\t\t\tr->"); W(fname); W(" = (int64_t)d.value;\n");
			break;
		case SCALAR_DOUBLE:
			W("\t\t\tr->"); W(fname); W(" = gremlin_f64_from_bits(d.value);\n");
			break;
		default: break;
		}
	}

	W("\t\t\tr->_has."); W(fname); W(" = true;\n");
	W("\t\t\tcontinue;\n\t\t}\n");
	return GREMLINP_OK;
}

/* ------------------------------------------------------------------------
 * Default-literal resolution for scalars. Converts `[default = X]` via the
 * validated const_convert layer and formats as a C literal usable
 * everywhere (comparison, designated initializer, NULL-return value).
 *
 * Returns a static C-string for the type zero when no explicit default is
 * declared; arena-allocated otherwise. NULL on conversion failure.
 * ------------------------------------------------------------------------ */

static bool
float_printout_is_integer(const char *s, int n)
{
	for (int i = 0; i < n; i++) {
		if (s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
		    s[i] == 'n' || s[i] == 'N') {	/* nan / inf identifiers */
			return false;
		}
	}
	return true;
}

const char *
gremlinc_scalar_resolve_default(struct gremlind_arena *arena,
				const struct gremlind_field *f,
				const struct scalar_info *si)
{
	if (!f->has_default) {
		return gremlinc_default_check_rhs(si->kind);
	}

	const struct gremlinp_const_parse_result *c = &f->default_value;
	char buf[64];
	int n = 0;

	switch (si->kind) {
	case SCALAR_INT32:
	case SCALAR_SINT32:
	case SCALAR_SFIXED32: {
		struct gremlinc_int32_convert_result r = gremlinc_const_to_int32(c);
		if (r.error != GREMLINP_OK) return NULL;
		/* Emit INT32_MIN via its stdint.h macro — the bare literal
		 * `-2147483648` has type `long` due to C's integer-literal
		 * sign-parsing rules, which triggers a compiler warning
		 * when assigned to int32_t. */
		if (r.value == INT32_MIN) return "INT32_MIN";
		n = snprintf(buf, sizeof buf, "%d", r.value);
		break;
	}
	case SCALAR_INT64:
	case SCALAR_SINT64:
	case SCALAR_SFIXED64: {
		struct gremlinc_int64_convert_result r = gremlinc_const_to_int64(c);
		if (r.error != GREMLINP_OK) return NULL;
		if (r.value == INT64_MIN) return "INT64_MIN";
		n = snprintf(buf, sizeof buf, "%lldLL", (long long)r.value);
		break;
	}
	case SCALAR_UINT32:
	case SCALAR_FIXED32: {
		struct gremlinc_uint32_convert_result r = gremlinc_const_to_uint32(c);
		if (r.error != GREMLINP_OK) return NULL;
		n = snprintf(buf, sizeof buf, "%uu", r.value);
		break;
	}
	case SCALAR_UINT64:
	case SCALAR_FIXED64: {
		struct gremlinc_uint64_convert_result r = gremlinc_const_to_uint64(c);
		if (r.error != GREMLINP_OK) return NULL;
		n = snprintf(buf, sizeof buf, "%lluULL", (unsigned long long)r.value);
		break;
	}
	case SCALAR_FLOAT: {
		struct gremlinc_float_convert_result r = gremlinc_const_to_float(c);
		if (r.error != GREMLINP_OK) return NULL;
		/* IEEE specials take C macros from <math.h> — caller decides
		 * whether to emit the include by consulting
		 * gremlind_file.needs_math_h. */
		if (r.value != r.value) return "(float)NAN";
		if (r.value > 3.4e38f) return "(float)INFINITY";
		if (r.value < -3.4e38f) return "(float)(-INFINITY)";
		n = snprintf(buf, sizeof buf, "%.9g", (double)r.value);
		if (n > 0 && float_printout_is_integer(buf, n) &&
		    n + 2 < (int)sizeof buf) {
			memcpy(buf + n, ".0", 2); n += 2;
		}
		if (n + 1 < (int)sizeof buf) { buf[n++] = 'f'; }
		break;
	}
	case SCALAR_DOUBLE: {
		struct gremlinc_double_convert_result r = gremlinc_const_to_double(c);
		if (r.error != GREMLINP_OK) return NULL;
		if (r.value != r.value) return "(double)NAN";
		if (r.value > 1e308) return "(double)INFINITY";
		if (r.value < -1e308) return "(double)(-INFINITY)";
		n = snprintf(buf, sizeof buf, "%.17g", r.value);
		if (n > 0 && float_printout_is_integer(buf, n) &&
		    n + 2 < (int)sizeof buf) {
			memcpy(buf + n, ".0", 2); n += 2;
		}
		break;
	}
	case SCALAR_BOOL: {
		struct gremlinc_bool_convert_result r = gremlinc_const_to_bool(c);
		if (r.error != GREMLINP_OK) return NULL;
		return r.value ? "true" : "false";
	}
	}

	if (n <= 0 || n >= (int)sizeof buf) return NULL;
	char *out = gremlind_arena_alloc(arena, (size_t)n + 1);
	if (out == NULL) return NULL;
	memcpy(out, buf, (size_t)n);
	out[n] = '\0';
	return out;
}

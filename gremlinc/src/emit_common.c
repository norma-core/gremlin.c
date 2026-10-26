#include <string.h>

#include "emit_common.h"

/* ------------------------------------------------------------------------
 * Scalar descriptor table — single source of truth for the mapping from
 * proto type name to C type, wire type, and emission tokens. Consumed by
 * every family module through gremlinc_scalar_lookup.
 *
 * The numeric `wire_type` mirrors the runtime's enum values (VARINT=0,
 * FIXED64=1, LEN_PREFIX=2, SGROUP=3, EGROUP=4, FIXED32=5) so codegen can
 * precompute packed tag constants without including gremlin.h.
 * ------------------------------------------------------------------------ */

#define WT_VARINT      0u
#define WT_FIXED64     1u
#define WT_FIXED32     5u

static const struct scalar_info SCALARS[] = {
	{ "int32",    5, "int32_t",  WT_VARINT,  "GREMLIN_WIRE_VARINT",  SCALAR_INT32    },
	{ "int64",    5, "int64_t",  WT_VARINT,  "GREMLIN_WIRE_VARINT",  SCALAR_INT64    },
	{ "uint32",   6, "uint32_t", WT_VARINT,  "GREMLIN_WIRE_VARINT",  SCALAR_UINT32   },
	{ "uint64",   6, "uint64_t", WT_VARINT,  "GREMLIN_WIRE_VARINT",  SCALAR_UINT64   },
	{ "sint32",   6, "int32_t",  WT_VARINT,  "GREMLIN_WIRE_VARINT",  SCALAR_SINT32   },
	{ "sint64",   6, "int64_t",  WT_VARINT,  "GREMLIN_WIRE_VARINT",  SCALAR_SINT64   },
	{ "fixed32",  7, "uint32_t", WT_FIXED32, "GREMLIN_WIRE_FIXED32", SCALAR_FIXED32  },
	{ "fixed64",  7, "uint64_t", WT_FIXED64, "GREMLIN_WIRE_FIXED64", SCALAR_FIXED64  },
	{ "sfixed32", 8, "int32_t",  WT_FIXED32, "GREMLIN_WIRE_FIXED32", SCALAR_SFIXED32 },
	{ "sfixed64", 8, "int64_t",  WT_FIXED64, "GREMLIN_WIRE_FIXED64", SCALAR_SFIXED64 },
	{ "double",   6, "double",   WT_FIXED64, "GREMLIN_WIRE_FIXED64", SCALAR_DOUBLE   },
	{ "float",    5, "float",    WT_FIXED32, "GREMLIN_WIRE_FIXED32", SCALAR_FLOAT    },
	{ "bool",     4, "bool",     WT_VARINT,  "GREMLIN_WIRE_VARINT",  SCALAR_BOOL     }
};
#define N_SCALARS (sizeof(SCALARS) / sizeof(SCALARS[0]))

const struct scalar_info *
gremlinc_scalar_lookup(const char *span, size_t len)
{
	for (size_t i = 0; i < N_SCALARS; i++) {
		if (SCALARS[i].proto_name_len == len &&
		    memcmp(SCALARS[i].proto_name, span, len) == 0) {
			return &SCALARS[i];
		}
	}
	return NULL;
}

bool
gremlinc_builtin_is_bytes(const char *span, size_t len)
{
	return (len == 6 && memcmp(span, "string", 6) == 0) ||
	       (len == 5 && memcmp(span, "bytes",  5) == 0);
}

bool
gremlinc_kind_uses_varint32(enum scalar_kind k)
{
	/* 5-byte varint path: values that always fit in uint32 on the wire.
	 * Signed int32 with negatives sign-extends to 10 bytes and needs the
	 * 64-bit primitive, so it's NOT in this set. */
	return k == SCALAR_UINT32 || k == SCALAR_SINT32 || k == SCALAR_BOOL;
}

unsigned
gremlinc_kind_alignment(enum scalar_kind k)
{
	switch (k) {
	case SCALAR_INT64:
	case SCALAR_UINT64:
	case SCALAR_SINT64:
	case SCALAR_FIXED64:
	case SCALAR_SFIXED64:
	case SCALAR_DOUBLE:
		return 8;
	case SCALAR_INT32:
	case SCALAR_UINT32:
	case SCALAR_SINT32:
	case SCALAR_FIXED32:
	case SCALAR_SFIXED32:
	case SCALAR_FLOAT:
		return 4;
	case SCALAR_BOOL:
		return 1;
	}
	return 1;	/* unreachable */
}

const char *
gremlinc_default_check_rhs(enum scalar_kind k)
{
	switch (k) {
	case SCALAR_DOUBLE:	return "0.0";
	case SCALAR_FLOAT:	return "0.0f";
	case SCALAR_BOOL:	return "false";
	default:		return "0";
	}
}

enum gremlinp_parsing_error
gremlinc_emit_iter_ok(struct gremlinc_writer *w, const char *reader_ty,
		      const char *fname, bool block)
{
	enum gremlinp_parsing_error err;

	W("/*@ predicate "); W(reader_ty); W("_"); W(fname); W("_iter_ok(");
	W(reader_ty); W("_"); W(fname); W("_iter it) =\n"
	  "      it.offset <= it.src_len &&\n");
	if (block) W("      it.block_end <= it.src_len &&\n");
	W("      (it.src_len == 0 || \\valid_read(it.src + (0 .. it.src_len - 1)));\n"
	  "*/\n\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_emit_iter_count_spec(struct gremlinc_writer *w, const char *fname)
{
	enum gremlinp_parsing_error err;

	W("/*@ requires r == \\null || \\valid_read(r);\n"
	  "    assigns \\nothing;\n"
	  "    ensures r != \\null ==> \\result == r->"); W(fname);
	W("_count;\n*/\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_emit_iter_begin_spec(struct gremlinc_writer *w, const char *reader_ty,
			      const char *reader_ok, const char *fname)
{
	enum gremlinp_parsing_error err;

	W("/*@ requires r == \\null || (\\valid_read(r) && "); W(reader_ok);
	W("(r, r->src_len) &&\n"
	  "               (r->src_len == 0 ||\n"
	  "                \\valid_read(r->src + (0 .. r->src_len - 1))));\n"
	  "    assigns \\nothing;\n"
	  "    ensures "); W(reader_ty); W("_"); W(fname); W("_iter_ok(\\result);\n"
	  "    ensures r != \\null && r->"); W(fname);
	W("_count > 0 ==>\n"
	  "              \\result.src == r->src && \\result.src_len == r->src_len &&\n"
	  "              \\result.count_remaining == r->"); W(fname); W("_count;\n"
	  "    ensures r == \\null || r->"); W(fname);
	W("_count == 0 ==>\n"
	  "              \\result.src_len == 0 && \\result.count_remaining == 0;\n*/\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_emit_iter_next_spec(struct gremlinc_writer *w, const char *reader_ty,
			     const char *fname, const char *out_extra,
			     const struct gremlind_message *child)
{
	enum gremlinp_parsing_error err;

	W("/*@ requires \\valid(it) && \\valid(out);\n"
	  "    requires "); W(reader_ty); W("_"); W(fname); W("_iter_ok(*it);\n"
	  "    requires \\separated(it, out, it->src + (0 .. it->src_len - 1));\n"
	  "    assigns *it, *out;\n"
	  "    ensures "); W(reader_ty); W("_"); W(fname); W("_iter_ok(*it);\n"
	  "    ensures it->src == \\old(it->src) && it->src_len == \\old(it->src_len);\n");
	if (out_extra != NULL) {
		W("    ensures "); W(out_extra); W(";\n");
	}
	if (child != NULL &&
	    (err = gremlinc_emit_child_reader_ok(w, child, "out")) != GREMLINP_OK)
		return err;
	if ((err = gremlinc_emit_error_set(w)) != GREMLINP_OK) return err;
	W("*/\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_emit_error_set(struct gremlinc_writer *w)
{
	enum gremlinp_parsing_error err;

	W("    ensures \\result == GREMLIN_OK || \\result == GREMLIN_ERROR_TRUNCATED ||\n"
	  "            \\result == GREMLIN_ERROR_OVERFLOW ||\n"
	  "            \\result == GREMLIN_ERROR_INVALID_WIRE_TYPE ||\n"
	  "            \\result == GREMLIN_ERROR_INVALID_FIELD_NUM;\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_emit_reader_ok_name(struct gremlinc_writer *w, const struct gremlind_message *m)
{
	enum gremlinp_parsing_error err;

	/* Without gremlinc_assign_reader_names the name is the unmangled one. */
	if (m->c_reader_ok != NULL) {
		W(m->c_reader_ok);
	} else {
		W(m->c_name); W("_reader_ok");
	}
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_emit_child_reader_ok(struct gremlinc_writer *w,
			      const struct gremlind_message *child, const char *out)
{
	enum gremlinp_parsing_error err;

	W("    ensures \\result == GREMLIN_OK ==>\n"
	  "              ");
	if ((err = gremlinc_emit_reader_ok_name(w, child)) != GREMLINP_OK) return err;
	W("("); W(out); W(", "); W(out);
	W("->src_len) &&\n"
	  "              ("); W(out); W("->src_len == 0 ||\n"
	  "               \\valid_read("); W(out); W("->src + (0 .. "); W(out);
	W("->src_len - 1)));\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_emit_iter_scan_spec(struct gremlinc_writer *w, const char *indent,
			     bool block)
{
	enum gremlinp_parsing_error err;

	W(indent); W("/*@ loop invariant it->offset <= it->src_len;\n");
	if (block) {
		W(indent); W("    loop invariant it->block_end <= it->src_len;\n");
		W(indent); W("    loop assigns it->offset, it->block_end;\n");
	} else {
		W(indent); W("    loop assigns it->offset;\n");
	}
	W(indent); W("    loop variant it->src_len - it->offset;\n");
	W(indent); W("*/\n");
	return GREMLINP_OK;
}

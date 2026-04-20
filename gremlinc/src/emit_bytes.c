#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gremlin.h"
#include "gremlinc/const_convert.h"

#include "emit_common.h"

/*
 * String + bytes emission.  Both are non-scalar length-delimited
 * primitives — they don't live in the scalar_info table; the
 * orchestrator dispatches to this module via
 * `gremlinc_builtin_is_bytes`.  C representation: `struct gremlin_bytes`
 * (const uint8_t *data + size_t len).  Proto semantics differ only in
 * intended use (UTF-8 vs raw bytes); wire + codegen treat them
 * identically.
 *
 * Encode: length-varint + raw bytes via gremlin_write_bytes.
 * Decode: gremlin_bytes_decode → zero-copy slice into the source buffer.
 * Default: proto3 implicit default is `{ NULL, 0 }`.  Proto2
 *          `[default = "hello"]` gets emitted as a compound literal
 *          with a C string literal + `sizeof(lit) - 1` for the length,
 *          so escape decoding happens at compile time.
 */

#define WT_LEN_PREFIX_NUM 2u

/* Emit the presence guard expression the size/encode functions wrap
 * their payload in.
 *
 *   - No `[default]`:  `m->fname.len > 0`  (proto3 implicit-presence:
 *                                           empty is indistinguishable
 *                                           from unset, so don't emit).
 *   - With `[default]`: emit iff the current bytes differ from the
 *                       literal default — either by length or by
 *                       content. Matches proto2's has-bit semantics
 *                       without requiring the writer to carry one,
 *                       since the literal default is by definition
 *                       the "unset" wire-level canonical.
 *
 * Memcmp is skipped when the default has length 0 (len == 0 already
 * matches) — we fall back to the same `len > 0` guard as the no-default
 * case. */
static enum gremlinp_parsing_error
emit_presence_guard(struct gremlinc_writer *w,
		    struct gremlind_arena *arena,
		    const struct gremlind_field *f,
		    const char *fname)
{
	enum gremlinp_parsing_error err = GREMLINP_OK;

	if (f->has_default) {
		struct gremlinc_bytes_convert_result r =
			gremlinc_const_to_bytes(&f->default_value, arena);
		if (r.error == GREMLINP_OK && r.escaped_len > 0) {
			/* Use `sizeof(LIT) - 1` for both the length compare
			 * and memcmp bound so C computes the decoded byte
			 * count at compile time. Raw char counting misses
			 * NUL escapes (`\0`, `\000`, `\x00`) — the proto
			 * default `"hel\000lo"` has 9 source chars but only
			 * 6 decoded bytes; sizeof() returns the right thing. */
			W("\tif (m->"); W(fname);
			W(".len != sizeof(\""); W(r.escaped); W("\") - 1");
			W(" || memcmp(m->"); W(fname);
			W(".data, \""); W(r.escaped); W("\", ");
			W("sizeof(\""); W(r.escaped); W("\") - 1) != 0) {\n");
			return err;
		}
	}

	W("\tif (m->"); W(fname); W(".len > 0) {\n");
	return err;
}

enum gremlinp_parsing_error
gremlinc_bytes_emit_size_field(struct gremlinc_writer *w,
			       struct gremlind_arena *arena,
			       const struct gremlind_field *f,
			       const char *fname)
{
	enum gremlinp_parsing_error err;

	uint32_t packed = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;
	unsigned tag_bytes = (unsigned)gremlin_varint_size(packed);

	err = emit_presence_guard(w, arena, f, fname);
	if (err != GREMLINP_OK) return err;
	W("\t\ts += "); WI((int32_t)tag_bytes);
	W("\n\t\t   + gremlin_varint_size(m->"); W(fname); W(".len)");
	W("\n\t\t   + m->"); W(fname); W(".len;\n");
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_bytes_emit_encode_field(struct gremlinc_writer *w,
				 struct gremlind_arena *arena,
				 const struct gremlind_field *f,
				 const char *fname)
{
	enum gremlinp_parsing_error err;

	uint32_t packed = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;

	err = emit_presence_guard(w, arena, f, fname);
	if (err != GREMLINP_OK) return err;
	W("\t\t_off = gremlin_varint32_encode_at(_buf, _off, "); WI((int32_t)packed); W("u);\n");
	W("\t\t_off = gremlin_varint_encode_at(_buf, _off, m->"); W(fname); W(".len);\n");
	W("\t\t_off = gremlin_write_bytes_at(_buf, _off, m->"); W(fname); W(".data, m->");
	W(fname); W(".len);\n");
	W("\t}\n");
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_bytes_emit_reader_arm(struct gremlinc_writer *w,
			       const struct gremlind_field *f,
			       const char *fname)
{
	enum gremlinp_parsing_error err;
	uint32_t packed_tag = ((uint32_t)f->parsed.index << 3) | WT_LEN_PREFIX_NUM;

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

const char *
gremlinc_bytes_resolve_default(struct gremlind_arena *arena,
			       const struct gremlind_field *f)
{
	if (!f->has_default) {
		return "(struct gremlin_bytes){ NULL, 0 }";
	}

	/* Convert proto string const → C-literal body via const_convert
	 * (which handles escape preservation + trigraph-breaking). */
	struct gremlinc_bytes_convert_result r =
		gremlinc_const_to_bytes(&f->default_value, arena);
	if (r.error != GREMLINP_OK) return NULL;

	size_t needed = r.escaped_len * 2 + 128;
	char *out = gremlind_arena_alloc(arena, needed);
	if (out == NULL) return NULL;
	int w = snprintf(out, needed,
		"(struct gremlin_bytes){ .data = (const uint8_t *)\"%s\", .len = sizeof(\"%s\") - 1 }",
		r.escaped, r.escaped);
	if (w <= 0 || w >= (int)needed) return NULL;
	return out;
}

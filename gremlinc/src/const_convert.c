#include <string.h>

#include "gremlinc/const_convert.h"
#include "gremlind/nodes.h"

/*
 * Proto-const → typed-C conversions for `[default = X]` on scalar fields.
 * Each function is a small set of kind-dispatched branches with explicit
 * range checks. ACSL contracts on the declarations make the postconditions
 * machine-checkable by Frama-C / WP.
 *
 * Integer targets accept CONST_INT and CONST_UINT.
 * Float / double accept CONST_INT, CONST_UINT, CONST_FLOAT (the parser
 * materialises `inf` / `-inf` / `nan` identifiers as CONST_FLOAT).
 * Bool accepts CONST_IDENTIFIER with span exactly "true" or "false".
 * Bytes/string conversions live below; CONST_STRING is rejected on
 * numeric targets.
 */

/*@ requires \valid_read(c);
    assigns  \nothing;
    ensures  \result.error == GREMLINP_OK ||
             \result.error == GREMLINP_ERROR_INVALID_FIELD_VALUE ||
             \result.error == GREMLINP_ERROR_OVERFLOW;
    ensures  \result.error != GREMLINP_OK ==> \result.value == 0;
*/
struct gremlinc_int32_convert_result
gremlinc_const_to_int32(const struct gremlinp_const_parse_result *c)
{
	struct gremlinc_int32_convert_result r = { 0, GREMLINP_OK };
	if (c->kind == GREMLINP_CONST_INT) {
		if (c->u.int_value < INT32_MIN || c->u.int_value > INT32_MAX) {
			r.error = GREMLINP_ERROR_OVERFLOW;
			return r;
		}
		r.value = (int32_t)c->u.int_value;
		return r;
	}
	if (c->kind == GREMLINP_CONST_UINT) {
		if (c->u.uint_value > (uint64_t)INT32_MAX) {
			r.error = GREMLINP_ERROR_OVERFLOW;
			return r;
		}
		r.value = (int32_t)c->u.uint_value;
		return r;
	}
	r.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
	return r;
}

/*@ requires \valid_read(c);
    assigns  \nothing;
    ensures  \result.error == GREMLINP_OK ||
             \result.error == GREMLINP_ERROR_INVALID_FIELD_VALUE ||
             \result.error == GREMLINP_ERROR_OVERFLOW;
    ensures  \result.error != GREMLINP_OK ==> \result.value == 0;
*/
struct gremlinc_int64_convert_result
gremlinc_const_to_int64(const struct gremlinp_const_parse_result *c)
{
	struct gremlinc_int64_convert_result r = { 0, GREMLINP_OK };
	if (c->kind == GREMLINP_CONST_INT) {
		r.value = c->u.int_value;
		return r;
	}
	if (c->kind == GREMLINP_CONST_UINT) {
		if (c->u.uint_value > (uint64_t)INT64_MAX) {
			r.error = GREMLINP_ERROR_OVERFLOW;
			return r;
		}
		r.value = (int64_t)c->u.uint_value;
		return r;
	}
	r.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
	return r;
}

/*@ requires \valid_read(c);
    assigns  \nothing;
    ensures  \result.error == GREMLINP_OK ||
             \result.error == GREMLINP_ERROR_INVALID_FIELD_VALUE ||
             \result.error == GREMLINP_ERROR_OVERFLOW;
    ensures  \result.error != GREMLINP_OK ==> \result.value == 0;
*/
struct gremlinc_uint32_convert_result
gremlinc_const_to_uint32(const struct gremlinp_const_parse_result *c)
{
	struct gremlinc_uint32_convert_result r = { 0, GREMLINP_OK };
	if (c->kind == GREMLINP_CONST_UINT) {
		if (c->u.uint_value > (uint64_t)UINT32_MAX) {
			r.error = GREMLINP_ERROR_OVERFLOW;
			return r;
		}
		r.value = (uint32_t)c->u.uint_value;
		return r;
	}
	if (c->kind == GREMLINP_CONST_INT) {
		if (c->u.int_value < 0 || c->u.int_value > (int64_t)UINT32_MAX) {
			r.error = GREMLINP_ERROR_OVERFLOW;
			return r;
		}
		r.value = (uint32_t)c->u.int_value;
		return r;
	}
	r.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
	return r;
}

/*@ requires \valid_read(c);
    assigns  \nothing;
    ensures  \result.error == GREMLINP_OK ||
             \result.error == GREMLINP_ERROR_INVALID_FIELD_VALUE ||
             \result.error == GREMLINP_ERROR_OVERFLOW;
    ensures  \result.error != GREMLINP_OK ==> \result.value == 0;
*/
struct gremlinc_uint64_convert_result
gremlinc_const_to_uint64(const struct gremlinp_const_parse_result *c)
{
	struct gremlinc_uint64_convert_result r = { 0, GREMLINP_OK };
	if (c->kind == GREMLINP_CONST_UINT) {
		r.value = c->u.uint_value;
		return r;
	}
	if (c->kind == GREMLINP_CONST_INT) {
		if (c->u.int_value < 0) {
			r.error = GREMLINP_ERROR_OVERFLOW;
			return r;
		}
		r.value = (uint64_t)c->u.int_value;
		return r;
	}
	r.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
	return r;
}

/*@ requires \valid_read(c);
    assigns  \nothing;
    ensures  \result.error == GREMLINP_OK ||
             \result.error == GREMLINP_ERROR_INVALID_FIELD_VALUE;
*/
struct gremlinc_float_convert_result
gremlinc_const_to_float(const struct gremlinp_const_parse_result *c)
{
	struct gremlinc_float_convert_result r = { 0.0f, GREMLINP_OK };
	if (c->kind == GREMLINP_CONST_FLOAT) {
		r.value = (float)c->u.float_value;
		return r;
	}
	if (c->kind == GREMLINP_CONST_INT) {
		r.value = (float)c->u.int_value;
		return r;
	}
	if (c->kind == GREMLINP_CONST_UINT) {
		r.value = (float)c->u.uint_value;
		return r;
	}
	r.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
	return r;
}

/*@ requires \valid_read(c);
    assigns  \nothing;
    ensures  \result.error == GREMLINP_OK ||
             \result.error == GREMLINP_ERROR_INVALID_FIELD_VALUE;
*/
struct gremlinc_double_convert_result
gremlinc_const_to_double(const struct gremlinp_const_parse_result *c)
{
	struct gremlinc_double_convert_result r = { 0.0, GREMLINP_OK };
	if (c->kind == GREMLINP_CONST_FLOAT) {
		r.value = c->u.float_value;
		return r;
	}
	if (c->kind == GREMLINP_CONST_INT) {
		r.value = (double)c->u.int_value;
		return r;
	}
	if (c->kind == GREMLINP_CONST_UINT) {
		r.value = (double)c->u.uint_value;
		return r;
	}
	r.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
	return r;
}

/*@ requires \valid_read(c);
    assigns  \nothing;
    ensures  \result.error == GREMLINP_OK ||
             \result.error == GREMLINP_ERROR_INVALID_FIELD_VALUE;
    ensures  \result.error != GREMLINP_OK ==> \result.value == \false;
*/
struct gremlinc_bool_convert_result
gremlinc_const_to_bool(const struct gremlinp_const_parse_result *c)
{
	struct gremlinc_bool_convert_result r = { false, GREMLINP_OK };
	if (c->kind != GREMLINP_CONST_IDENTIFIER) {
		r.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
		return r;
	}
	if (c->u.span.length == 4 && memcmp(c->u.span.start, "true", 4) == 0) {
		r.value = true;
		return r;
	}
	if (c->u.span.length == 5 && memcmp(c->u.span.start, "false", 5) == 0) {
		r.value = false;
		return r;
	}
	r.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
	return r;
}

struct gremlinc_int32_convert_result
gremlinc_const_to_enum(const struct gremlinp_const_parse_result *c,
		       const struct gremlind_enum *target)
{
	/* INT / UINT form: validate as int32 (same rules as int32 default).
	 * Generated code coerces the returned integer to the enum typedef
	 * at the assignment site. */
	if (c->kind == GREMLINP_CONST_INT || c->kind == GREMLINP_CONST_UINT) {
		return gremlinc_const_to_int32(c);
	}

	/* IDENTIFIER form: look the name up in the target enum's values,
	 * return the matching declared integer. */
	struct gremlinc_int32_convert_result r = { 0, GREMLINP_OK };
	if (c->kind != GREMLINP_CONST_IDENTIFIER || target == NULL) {
		r.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
		return r;
	}
	for (size_t i = 0; i < target->values.count; i++) {
		const struct gremlinp_enum_field_parse_result *v =
			&target->values.items[i].parsed;
		if (v->name_length == c->u.span.length &&
		    memcmp(v->name_start, c->u.span.start, v->name_length) == 0) {
			r.value = v->index;
			return r;
		}
	}
	r.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
	return r;
}

struct gremlinc_bytes_convert_result
gremlinc_const_to_bytes(const struct gremlinp_const_parse_result *c,
			struct gremlind_arena *arena)
{
	struct gremlinc_bytes_convert_result r = { NULL, 0, GREMLINP_OK };
	if (c->kind != GREMLINP_CONST_STRING) {
		r.error = GREMLINP_ERROR_INVALID_FIELD_VALUE;
		return r;
	}

	const char *span = c->u.span.start;
	size_t span_len  = c->u.span.length;

	/* Worst case: every byte doubles (bare `?` → `\?`). +1 for NUL. */
	size_t cap = span_len * 2 + 1;
	char *out = gremlind_arena_alloc(arena, cap);
	if (out == NULL) {
		r.error = GREMLINP_ERROR_OUT_OF_MEMORY;
		return r;
	}

	size_t o = 0;
	for (size_t i = 0; i < span_len; i++) {
		if (span[i] == '\\' && i + 1 < span_len) {
			/* Existing escape sequence — copy both bytes verbatim
			 * so the escape stays intact (proto and C share the
			 * common escape set: \n, \t, \r, \", \', \\, \xHH,
			 * octals, etc.). */
			out[o++] = span[i];
			out[o++] = span[i + 1];
			i++;
		} else if (span[i] == '?') {
			/* Escape bare `?` to `\?` — preserves meaning,
			 * breaks any `??X` trigraph before the preprocessor
			 * sees it. */
			out[o++] = '\\';
			out[o++] = '?';
		} else {
			out[o++] = span[i];
		}
	}
	out[o] = '\0';

	r.escaped = out;
	r.escaped_len = o;
	return r;
}

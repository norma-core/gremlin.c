#ifndef _GREMLINC_CONST_CONVERT_H_
#define _GREMLINC_CONST_CONVERT_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "gremlind/arena.h"
#include "gremlinp/errors.h"
#include "gremlinp/lexems.h"

/*
 * Proto constant → typed C value conversions, for `[default = X]`
 * options on scalar fields.
 *
 * Each function takes a parser-produced `gremlinp_const_parse_result`
 * and a compile-time-known target C type, validates that the const's
 * kind + range are compatible with the target, and returns the
 * converted value paired with an error code. Every function is pure,
 * assigns nothing, and has ACSL contracts discharged by Frama-C WP.
 *
 * Error codes:
 *   GREMLINP_OK                          — conversion succeeded.
 *   GREMLINP_ERROR_INVALID_FIELD_VALUE   — const's kind isn't compatible
 *                                          with the target type
 *                                          (e.g. string literal for int).
 *   GREMLINP_ERROR_OVERFLOW              — kind is right but value is out
 *                                          of range for the target.
 *
 * Coverage:
 *   - Integer targets (int32/int64/uint32/uint64): accept CONST_INT and
 *     CONST_UINT; range-check against the target's min/max.
 *   - Float / double: accept CONST_FLOAT, CONST_INT, CONST_UINT; no
 *     range check (IEEE-754 converts everything lossy-but-defined).
 *     `inf` / `-inf` / `nan` arrive as CONST_FLOAT (the parser
 *     resolves the identifiers to their IEEE-754 values), so they
 *     hit the same path as ordinary floating-point literals.
 *   - Bool: accept CONST_IDENTIFIER with span exactly "true" or "false".
 *
 * On error, the `value` field is zero (type-appropriate).
 */

struct gremlinc_int32_convert_result {
	int32_t				value;
	enum gremlinp_parsing_error	error;
};

struct gremlinc_int64_convert_result {
	int64_t				value;
	enum gremlinp_parsing_error	error;
};

struct gremlinc_uint32_convert_result {
	uint32_t			value;
	enum gremlinp_parsing_error	error;
};

struct gremlinc_uint64_convert_result {
	uint64_t			value;
	enum gremlinp_parsing_error	error;
};

struct gremlinc_float_convert_result {
	float				value;
	enum gremlinp_parsing_error	error;
};

struct gremlinc_double_convert_result {
	double				value;
	enum gremlinp_parsing_error	error;
};

struct gremlinc_bool_convert_result {
	bool				value;
	enum gremlinp_parsing_error	error;
};

struct gremlinc_int32_convert_result
gremlinc_const_to_int32(const struct gremlinp_const_parse_result *c);

struct gremlinc_int64_convert_result
gremlinc_const_to_int64(const struct gremlinp_const_parse_result *c);

struct gremlinc_uint32_convert_result
gremlinc_const_to_uint32(const struct gremlinp_const_parse_result *c);

struct gremlinc_uint64_convert_result
gremlinc_const_to_uint64(const struct gremlinp_const_parse_result *c);

struct gremlinc_float_convert_result
gremlinc_const_to_float(const struct gremlinp_const_parse_result *c);

struct gremlinc_double_convert_result
gremlinc_const_to_double(const struct gremlinp_const_parse_result *c);

struct gremlinc_bool_convert_result
gremlinc_const_to_bool(const struct gremlinp_const_parse_result *c);

/*
 * Bytes/string conversion: accept CONST_STRING, emit the span's content
 * as a C string-literal body (no surrounding quotes) with every bare `?`
 * escaped to `\?`. The escape preserves the character's meaning in both
 * proto and C, and prevents the preprocessor from recognising any `??X`
 * trigraph sequence. Caller wraps with `"..."` + computes length via
 * `sizeof(lit) - 1`. Result is arena-allocated and NUL-terminated.
 *
 * Existing `\X` escape sequences in the source span are preserved
 * verbatim (both bytes copied without re-escaping) since proto and C
 * share the common escape set.
 */
struct gremlinc_bytes_convert_result {
	const char			*escaped;	/* arena-allocated NUL-terminated */
	size_t				 escaped_len;
	enum gremlinp_parsing_error	 error;
};

struct gremlinc_bytes_convert_result
gremlinc_const_to_bytes(const struct gremlinp_const_parse_result *c,
			struct gremlind_arena *arena);

/*
 * Enum default: accepts CONST_INT / CONST_UINT (numeric literal;
 * validated against int32 range), or CONST_IDENTIFIER (proto enum
 * value name — looked up against `target->values` and converted to
 * its declared integer).  Result is the integer (int32); generated
 * code coerces to the enum typedef on assignment.
 */
struct gremlind_enum;	/* fwd — declared in gremlind/nodes.h */

struct gremlinc_int32_convert_result
gremlinc_const_to_enum(const struct gremlinp_const_parse_result *c,
		       const struct gremlind_enum *target);

#endif /* !_GREMLINC_CONST_CONVERT_H_ */

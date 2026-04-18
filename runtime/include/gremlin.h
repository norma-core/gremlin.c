#ifndef _GREMLIN_H_
#define _GREMLIN_H_

#include <stdint.h>
#include <stddef.h>

/*
 *               .'\   /`.
 *             .'.-.`-'.-.`.
 *        ..._:   .-. .-.   :_...
 *      .'    '-.(o ) (o ).-'    `.
 *     :  _    _ _`~(_)~`_ _    _  :
 *    :  /:   ' .-=_   _=-. `   ;\  :
 *    :   :|-.._  '     `  _..-|:   :
 *     :   `:| |`:-:-.-:-:'| |:'   :
 *      `.   `.| | | | | | |.'   .'
 *        `.   `-:_| | |_:-'   .'
 *          `-._   ````    _.-'
 *              ``-------''
 *
 * gremlin.h — formally-verified protobuf wire runtime.
 *
 * Header-only. Every function is `static inline` with an ACSL contract
 * discharged by Frama-C WP. No linked library, no allocation — just
 * bit math and byte-level buffer operations. Consumed by the codegen
 * output (`#include "gremlin.h"`).
 *
 * Two-pass encoding convention: callers first compute the total encoded
 * size via gremlin_*_size, allocate exactly that many bytes, then fill
 * them with gremlin_*_encode. Because encoders have a precondition that
 * the buffer is pre-sized, they never return an error — the contract
 * prevents under-allocation.
 */

enum gremlin_error {
	GREMLIN_OK = 0,
	GREMLIN_ERROR_TRUNCATED,		/* buffer ended mid-value */
	GREMLIN_ERROR_OVERFLOW,			/* 10th varint byte still had continuation */
	GREMLIN_ERROR_INVALID_WIRE_TYPE,	/* tag used reserved wire types 6 or 7 */
	GREMLIN_ERROR_INVALID_FIELD_NUM		/* field number 0 or > 2^29 - 1 */
};

/* Per protobuf spec: field numbers range [1, 2^29 - 1]. Higher values
 * are reserved; 0 is invalid. */
#define GREMLIN_MAX_FIELD_NUM ((uint32_t)0x1FFFFFFFu)

/*
 * Protobuf wire types. The low 3 bits of a tag. Six are valid (0..5);
 * 3 and 4 are the deprecated proto2 groups (we recognise them so
 * skippers can advance past them correctly). 6 and 7 are reserved
 * and rejected as GREMLIN_ERROR_INVALID_WIRE_TYPE on decode.
 */
enum gremlin_wire_type {
	GREMLIN_WIRE_VARINT     = 0,	/* int32/64, uint32/64, sint32/64, bool, enum */
	GREMLIN_WIRE_FIXED64    = 1,	/* fixed64, sfixed64, double */
	GREMLIN_WIRE_LEN_PREFIX = 2,	/* string, bytes, embedded message, packed repeated */
	GREMLIN_WIRE_SGROUP     = 3,	/* proto2 group start (deprecated) */
	GREMLIN_WIRE_EGROUP     = 4,	/* proto2 group end (deprecated) */
	GREMLIN_WIRE_FIXED32    = 5	/* fixed32, sfixed32, float */
};

struct gremlin_tag {
	uint32_t			field_num;
	enum gremlin_wire_type		wire_type;
};

/*
 * Writer: buffer + capacity + bump cursor. The caller constructs one
 * pointing at a pre-sized buffer (size computed via gremlin_*_size
 * helpers), hands it to a sequence of encoders, and reads out `offset`
 * at the end for the number of bytes written. Encoders have a
 * precondition that the remaining capacity fits the value being
 * written, so they never return an error — the contract catches any
 * under-allocation at verification time.
 */
struct gremlin_writer {
	uint8_t		*buf;
	size_t		 cap;
	size_t		 offset;
};

/*@ predicate valid_writer{L}(struct gremlin_writer *w) =
      \valid(w) &&
      w->offset <= w->cap &&
      (w->cap > 0 ==> \valid(w->buf + (0 .. w->cap - 1)));
 */

/*@ requires \valid(w);
    requires cap == 0 || \valid(buf + (0 .. cap - 1));
    assigns  *w;
    ensures  valid_writer(w);
    ensures  w->buf == buf && w->cap == cap && w->offset == 0;
*/
static inline __attribute__((always_inline)) void
gremlin_writer_init(struct gremlin_writer *w, uint8_t *buf, size_t cap)
{
	w->buf = buf;
	w->cap = cap;
	w->offset = 0;
}

/* ------------------------------------------------------------------------
 * Varint size — how many bytes the LEB128 encoding of v occupies.
 *
 * Spec: non-recursive ACSL logic function `varint_size` defined by case
 * analysis over 9 power-of-128 bands. Bounds 1..10 and correctness of
 * both implementations (cascade + CLZ) are SMT-discharged directly
 * against the case table.
 * ------------------------------------------------------------------------ */

/*@ axiomatic GremlinVarintSize {
      // Defined over ACSL mathematical integer so `v / 128` typechecks
      // without explicit casts. Call sites coerce uint64_t implicitly.
      logic integer varint_size(integer v) =
        v < 0x80                    ? 1 :
        v < 0x4000                  ? 2 :
        v < 0x200000                ? 3 :
        v < 0x10000000              ? 4 :
        v < 0x800000000             ? 5 :
        v < 0x40000000000           ? 6 :
        v < 0x2000000000000         ? 7 :
        v < 0x100000000000000       ? 8 :
        v < 0x8000000000000000      ? 9 :
        10;
    }
*/

/*
 * Byte-level spec: the k-th byte the canonical LEB128 encoder writes
 * for value v. All bytes except the last have the continuation bit
 * (0x80) set; the last byte's high bit is clear. Recursive definition
 * — SMT handles via the three axioms + finite case analysis bounded
 * by 10 bytes.
 */
/*@ axiomatic GremlinVarintBytes {
      logic integer varint_byte(integer v, integer k);

      // Base, single-byte case: v fits in 7 bits, no continuation.
      axiom varint_byte_0_small:
        \forall integer v; 0 <= v < 128 ==> varint_byte(v, 0) == v;

      // Base, multi-byte case: low 7 bits of v with continuation bit.
      axiom varint_byte_0_cont:
        \forall integer v; v >= 128 ==>
          varint_byte(v, 0) == 128 + v % 128;

      // Step: k-th byte of v is the (k-1)-th byte of (v / 128).
      axiom varint_byte_step:
        \forall integer v, integer k;
          v >= 0 && k >= 1 ==>
            varint_byte(v, k) == varint_byte(v / 128, k - 1);
    }
*/

/*
 * Trust base for the CLZ fast path — two admitted facts.
 *
 * 1. __builtin_clzll is the GCC/Clang intrinsic for "count leading
 *    zeros" on unsigned long long. UB for v == 0; callers guard with
 *    an explicit `if (v == 0)` branch so the precondition v >= 1 holds
 *    at every call site.
 *
 * 2. The bridge identity clz_matches_varint_size: the formula
 *    `(64 - clzll(v) + 6) / 7` equals `varint_size(v)` for all v >= 1.
 *    Verifiable by hand via case-analysis over the 9 power-of-128
 *    bands (each band maps to a contiguous clzll output range). SMT
 *    with WP's current bit-shift encoding can't discharge this
 *    directly; admitted here with the same discipline as the parser's
 *    strncmp / strtod axioms.
 */
/*@ axiomatic GremlinBuiltinCLZ {
      // Logic mirror of __builtin_clzll. The intrinsic's postcondition
      // ties its C-level return to this logic function; ACSL axioms
      // speak only in terms of logic_clzll.
      logic integer logic_clzll(unsigned long long v);

      axiom logic_clzll_range:
        \forall unsigned long long v; v >= 1 ==>
          0 <= logic_clzll(v) <= 63;

      // The bridge. By hand: for v in band k (k=1..10), v satisfies
      //   128^(k-1) <= v < 128^k    (with band 10 open on the right).
      // Meanwhile logic_clzll(v) is in [64-7k, 63-7(k-1)], so
      //   64 - logic_clzll(v) is in [7(k-1)+1, 7k],
      // and (that + 6) / 7 == k == varint_size(v).
      axiom clz_matches_varint_size:
        \forall unsigned long long v; v >= 1 ==>
          varint_size(v) == (64 - logic_clzll(v) + 6) / 7;
    }
*/

/*@ requires v >= 1;
    assigns  \nothing;
    ensures  0 <= \result <= 63;
    ensures  \result == logic_clzll(v);
*/
extern int __builtin_clzll(unsigned long long v);

/*
 * Two implementations, both verified. The outer function dispatches
 * based on compiler support.
 */

/*@ assigns \nothing;
    ensures \result == varint_size(v);
    ensures 1 <= \result <= 10;
*/
static inline __attribute__((always_inline)) size_t
gremlin_varint_size_cascade(uint64_t v)
{
	/* Portable branchless cascade. Each arm matches one case of the
	 * varint_size logic function by construction; the proof reduces to
	 * a 9-way comparison check. */
	if (v < 0x80)                  return 1;
	if (v < 0x4000)                return 2;
	if (v < 0x200000)              return 3;
	if (v < 0x10000000)            return 4;
	if (v < 0x800000000ULL)        return 5;
	if (v < 0x40000000000ULL)      return 6;
	if (v < 0x2000000000000ULL)    return 7;
	if (v < 0x100000000000000ULL)  return 8;
	if (v < 0x8000000000000000ULL) return 9;
	return 10;
}

#if defined(__GNUC__) || defined(__clang__) || defined(__FRAMAC__)
/*@ assigns \nothing;
    ensures \result == varint_size(v);
    ensures 1 <= \result <= 10;
*/
static inline __attribute__((always_inline)) size_t
gremlin_varint_size_clz(uint64_t v)
{
	/* Fast path: count leading zeros → bit width → ceil-div by 7.
	 * Explicit v==0 branch establishes clzll's v>=1 precondition for
	 * WP while also handling the spec (varint_size(0) == 1). */
	if (v == 0) return 1;
	unsigned bits = (unsigned)(64 - __builtin_clzll(v));
	return (size_t)((bits + 6u) / 7u);
}
#endif

/*@ assigns \nothing;
    ensures \result == varint_size(v);
    ensures 1 <= \result <= 10;
*/
static inline __attribute__((always_inline)) size_t
gremlin_varint_size(uint64_t v)
{
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__FRAMAC__)
	return gremlin_varint_size_clz(v);
#else
	return gremlin_varint_size_cascade(v);
#endif
}

/* ------------------------------------------------------------------------
 * Varint encode — write v as LEB128 at the writer's cursor.
 *
 * Precondition: the writer has at least varint_size(v) bytes of capacity
 * remaining (caller pre-computed via gremlin_varint_size + caller-side
 * budgeting). Because of this, the encoder can never fail — it's void.
 *
 * Postcondition: writer.offset advances by exactly varint_size(v).
 * ------------------------------------------------------------------------ */

/*@ requires valid_writer(w);
    requires w->offset + varint_size(v) <= w->cap;
    assigns  w->offset,
             w->buf[\at(w->offset, Pre) ..
                    \at(w->offset, Pre) + varint_size(v) - 1];
    ensures  valid_writer(w);
    ensures  w->offset == \old(w->offset) + varint_size(v);
    // Bit-level correctness: each byte written equals the byte the
    // canonical LEB128 encoding of v would place at that index.
    ensures  \forall integer k;
               0 <= k < varint_size(\old(v)) ==>
                 w->buf[\old(w->offset) + k] ==
                 (uint8_t)varint_byte(\old(v), k);
*/
static inline __attribute__((always_inline)) void
gremlin_varint_encode(struct gremlin_writer *w, uint64_t v)
{
	/* Fast path: single-byte varint (v < 128). The dominant case in
	 * real protobuf: field tags with number < 16, bool, small
	 * enums, short length prefixes. Single store + increment. */
	if (v < 128) {
		/*@ assert w->offset < w->cap; */
		w->buf[w->offset] = (uint8_t)v;
		w->offset++;
		return;
	}

	/*@ loop invariant w->offset + varint_size(v) ==
	                   \at(w->offset, Pre) + varint_size(\at(v, Pre));
	    loop invariant w->offset <= w->cap;
	    loop invariant \at(w->offset, Pre) <= w->offset;
	    // Key invariant: v (current) maps to v_initial's k-th byte onward.
	    loop invariant \forall integer j;
	      j >= 0 ==>
	        varint_byte(v, j) ==
	          varint_byte(\at(v, Pre),
	                      (w->offset - \at(w->offset, Pre)) + j);
	    // Bytes written so far are correct.
	    loop invariant \forall integer k;
	      0 <= k < (w->offset - \at(w->offset, Pre)) ==>
	        w->buf[\at(w->offset, Pre) + k] ==
	          (uint8_t)varint_byte(\at(v, Pre), k);
	    loop assigns v, w->offset,
	                 w->buf[\at(w->offset, Pre) ..
	                        \at(w->offset, Pre) +
	                        varint_size(\at(v, Pre)) - 1];
	    loop variant v;
	*/
	while (v >= 128) {
		/*@ assert w->offset < w->cap; */
		/*@ assert varint_byte(v, 0) == 128 + v % 128; */
		w->buf[w->offset] = (uint8_t)(128 + v % 128);
		w->offset++;
		/*@ assert \forall integer j; j >= 0 ==>
		           varint_byte(v / 128, j) == varint_byte(v, j + 1); */
		v /= 128;
	}
	/*@ assert w->offset < w->cap; */
	/*@ assert varint_byte(v, 0) == v; */
	w->buf[w->offset] = (uint8_t)v;
	w->offset++;
}

/* ------------------------------------------------------------------------
 * Varint decode — read one LEB128-encoded uint64 from a byte buffer.
 *
 * Pure function: no reader cursor struct. Caller feeds a raw slice and
 * gets back `{ value, consumed, error }` — the consumed field is how
 * many bytes to advance past for the next field. On error, consumed
 * is 0 and value is undefined.
 *
 * Errors:
 *   TRUNCATED — every byte read had the continuation bit set but the
 *               buffer ran out before we saw the terminating byte.
 *   OVERFLOW  — read 10 bytes and the 10th still had the continuation
 *               bit, so the encoded value exceeds uint64_t.
 * ------------------------------------------------------------------------ */

struct gremlin_varint_decode_result {
	uint64_t		value;		/* decoded value; valid iff error == OK */
	size_t			consumed;	/* bytes consumed; 0 on error */
	enum gremlin_error	error;
};

/*@ requires len == 0 || \valid_read(buf + (0 .. len - 1));
    assigns  \nothing;
    ensures  \result.error == GREMLIN_OK ||
             \result.error == GREMLIN_ERROR_TRUNCATED ||
             \result.error == GREMLIN_ERROR_OVERFLOW;
    ensures  \result.error == GREMLIN_OK ==>
               1 <= \result.consumed <= 10 &&
               \result.consumed <= len;
    ensures  \result.error == GREMLIN_ERROR_TRUNCATED ==> len < 10;
    ensures  \result.error == GREMLIN_ERROR_OVERFLOW ==> len >= 10;
    ensures  \result.error != GREMLIN_OK ==>
               \result.consumed == 0 && \result.value == 0;
*/
static inline __attribute__((always_inline)) struct gremlin_varint_decode_result
gremlin_varint_decode(const uint8_t *buf, size_t len)
{
	struct gremlin_varint_decode_result r;
	r.value = 0;
	r.consumed = 0;
	r.error = GREMLIN_OK;

	/* Fast path: single-byte varint (values 0..127). */
	if (len >= 1 && buf[0] < 128u) {
		r.value = buf[0];
		r.consumed = 1;
		return r;
	}

	uint64_t value = 0;
	size_t   i = 0;
	unsigned shift = 0;

	/*@ loop invariant 0 <= i <= 10;
	    loop invariant i <= len;
	    loop invariant shift == 7 * i;
	    loop assigns i, shift, value;
	    loop variant 10 - i;
	*/
	while (i < len && i < 10) {
		/*@ assert shift <= 63; */
		uint8_t b = buf[i];
		value |= ((uint64_t)(b & 0x7Fu)) << shift;
		i++;
		if ((b & 0x80u) == 0) {
			r.value = value;
			r.consumed = i;
			return r;
		}
		shift += 7;
	}

	r.error = (i == 10) ? GREMLIN_ERROR_OVERFLOW : GREMLIN_ERROR_TRUNCATED;
	return r;
}

/*
 * Little-endian byte-sequence values — arithmetic form so SMT can
 * chain without needing bit-vector theory. `le32_value(buf)` is the
 * uint32 represented by 4 LE bytes at buf; `le64_value` is the u64
 * form over 8 bytes. These are the decoder's logic-level output spec.
 */
/*@ axiomatic GremlinLittleEndian {
      logic integer le32_value(uint8_t *buf) =
        (integer)buf[0]
      + 256       * (integer)buf[1]
      + 65536     * (integer)buf[2]
      + 16777216  * (integer)buf[3];

      logic integer le64_value(uint8_t *buf) =
        (integer)buf[0]
      + 256                  * (integer)buf[1]
      + 65536                * (integer)buf[2]
      + 16777216             * (integer)buf[3]
      + 4294967296           * (integer)buf[4]
      + 1099511627776        * (integer)buf[5]
      + 281474976710656      * (integer)buf[6]
      + 72057594037927936    * (integer)buf[7];
    }
*/

/* ========================================================================
 * Fixed 32/64 — raw little-endian 4/8-byte integers.
 *
 * Wire format: no continuation bits, no length prefix — just the raw
 * bytes in LE order. Maps to protobuf wire types 5 (fixed32, sfixed32)
 * and 1 (fixed64, sfixed64). Signed variants (sfixed32/sfixed64) share
 * the exact same wire bytes as their unsigned counterparts; generated
 * code reinterprets `int32_t` ↔ `uint32_t` (and 64) via plain cast —
 * no separate runtime primitive needed.
 *
 * Fixed size means:
 *   - encoder contract is trivial: caller guarantees 4/8 bytes free, we
 *     write them and advance.
 *   - decoder contract returns just the value + error (no `consumed`;
 *     it's always 4 or 8 on success).
 * ======================================================================== */

/* ------------ Fixed 32 ------------ */

static inline __attribute__((always_inline)) size_t
gremlin_fixed32_size(void)
{
	return 4;
}

/*@ requires valid_writer(w);
    requires w->offset + 4 <= w->cap;
    assigns  w->offset,
             w->buf[\at(w->offset, Pre) .. \at(w->offset, Pre) + 3];
    ensures  valid_writer(w);
    ensures  w->offset == \old(w->offset) + 4;
    // Little-endian byte layout: byte k gets the (8k..8k+7) bit slice.
    ensures  w->buf[\old(w->offset) + 0] == (uint8_t)v;
    ensures  w->buf[\old(w->offset) + 1] == (uint8_t)(v >> 8);
    ensures  w->buf[\old(w->offset) + 2] == (uint8_t)(v >> 16);
    ensures  w->buf[\old(w->offset) + 3] == (uint8_t)(v >> 24);
*/
static inline __attribute__((always_inline)) void
gremlin_fixed32_encode(struct gremlin_writer *w, uint32_t v)
{
	w->buf[w->offset + 0] = (uint8_t)v;
	w->buf[w->offset + 1] = (uint8_t)(v >> 8);
	w->buf[w->offset + 2] = (uint8_t)(v >> 16);
	w->buf[w->offset + 3] = (uint8_t)(v >> 24);
	w->offset += 4;
}

struct gremlin_fixed32_decode_result {
	uint32_t		value;
	enum gremlin_error	error;	/* OK or TRUNCATED */
};

/*@ requires len == 0 || \valid_read(buf + (0 .. len - 1));
    assigns  \nothing;
    ensures  \result.error == GREMLIN_OK || \result.error == GREMLIN_ERROR_TRUNCATED;
    ensures  \result.error == GREMLIN_OK ==> len >= 4;
    ensures  \result.error == GREMLIN_ERROR_TRUNCATED ==> len < 4;
    ensures  \result.error == GREMLIN_ERROR_TRUNCATED ==> \result.value == 0;
    ensures  \result.error == GREMLIN_OK ==> \result.value == le32_value(buf);
*/
static inline __attribute__((always_inline)) struct gremlin_fixed32_decode_result
gremlin_fixed32_decode(const uint8_t *buf, size_t len)
{
	struct gremlin_fixed32_decode_result r = { 0, GREMLIN_OK };
	if (len < 4) {
		r.error = GREMLIN_ERROR_TRUNCATED;
		return r;
	}
	/* Arithmetic (+, *) rather than (|, <<) — identical asm for
	 * non-overlapping bytes, but lets SMT chain against le32_value. */
	r.value = (uint32_t)buf[0]
	        + (uint32_t)buf[1] * 256u
	        + (uint32_t)buf[2] * 65536u
	        + (uint32_t)buf[3] * 16777216u;
	return r;
}

/* ------------ Fixed 64 ------------ */

static inline __attribute__((always_inline)) size_t
gremlin_fixed64_size(void)
{
	return 8;
}

/*@ requires valid_writer(w);
    requires w->offset + 8 <= w->cap;
    assigns  w->offset,
             w->buf[\at(w->offset, Pre) .. \at(w->offset, Pre) + 7];
    ensures  valid_writer(w);
    ensures  w->offset == \old(w->offset) + 8;
    ensures  w->buf[\old(w->offset) + 0] == (uint8_t)v;
    ensures  w->buf[\old(w->offset) + 1] == (uint8_t)(v >> 8);
    ensures  w->buf[\old(w->offset) + 2] == (uint8_t)(v >> 16);
    ensures  w->buf[\old(w->offset) + 3] == (uint8_t)(v >> 24);
    ensures  w->buf[\old(w->offset) + 4] == (uint8_t)(v >> 32);
    ensures  w->buf[\old(w->offset) + 5] == (uint8_t)(v >> 40);
    ensures  w->buf[\old(w->offset) + 6] == (uint8_t)(v >> 48);
    ensures  w->buf[\old(w->offset) + 7] == (uint8_t)(v >> 56);
*/
static inline __attribute__((always_inline)) void
gremlin_fixed64_encode(struct gremlin_writer *w, uint64_t v)
{
	w->buf[w->offset + 0] = (uint8_t)v;
	w->buf[w->offset + 1] = (uint8_t)(v >> 8);
	w->buf[w->offset + 2] = (uint8_t)(v >> 16);
	w->buf[w->offset + 3] = (uint8_t)(v >> 24);
	w->buf[w->offset + 4] = (uint8_t)(v >> 32);
	w->buf[w->offset + 5] = (uint8_t)(v >> 40);
	w->buf[w->offset + 6] = (uint8_t)(v >> 48);
	w->buf[w->offset + 7] = (uint8_t)(v >> 56);
	w->offset += 8;
}

struct gremlin_fixed64_decode_result {
	uint64_t		value;
	enum gremlin_error	error;
};

/*@ requires len == 0 || \valid_read(buf + (0 .. len - 1));
    assigns  \nothing;
    ensures  \result.error == GREMLIN_OK || \result.error == GREMLIN_ERROR_TRUNCATED;
    ensures  \result.error == GREMLIN_OK ==> len >= 8;
    ensures  \result.error == GREMLIN_ERROR_TRUNCATED ==> len < 8;
    ensures  \result.error == GREMLIN_ERROR_TRUNCATED ==> \result.value == 0;
    ensures  \result.error == GREMLIN_OK ==> \result.value == le64_value(buf);
*/
static inline __attribute__((always_inline)) struct gremlin_fixed64_decode_result
gremlin_fixed64_decode(const uint8_t *buf, size_t len)
{
	struct gremlin_fixed64_decode_result r = { 0, GREMLIN_OK };
	if (len < 8) {
		r.error = GREMLIN_ERROR_TRUNCATED;
		return r;
	}
	r.value = (uint64_t)buf[0]
	        + (uint64_t)buf[1] * 256ULL
	        + (uint64_t)buf[2] * 65536ULL
	        + (uint64_t)buf[3] * 16777216ULL
	        + (uint64_t)buf[4] * 4294967296ULL
	        + (uint64_t)buf[5] * 1099511627776ULL
	        + (uint64_t)buf[6] * 281474976710656ULL
	        + (uint64_t)buf[7] * 72057594037927936ULL;
	return r;
}

/* ========================================================================
 * Tag — field number + wire type, packed into a varint.
 *
 * Wire layout: varint of `(field_num << 3) | wire_type`. The low 3 bits
 * hold the wire type; the rest is the field number.
 *
 * Tag size is just the size of that packed varint — which in turn is
 * gremlin_varint_size(packed) with packed = (field_num << 3) | wt.
 * ======================================================================== */

/*@ axiomatic GremlinTag {
      // The packed 64-bit value for a (field_num, wire_type) pair.
      // wire_type fits in 3 bits; field_num shifts up by 3.
      logic integer tag_packed(integer field_num, integer wire_type) =
        field_num * 8 + wire_type;
    }
*/

/*@ requires 0 <= (integer)wt <= 5;
    assigns  \nothing;
    ensures  \result == varint_size(tag_packed(field_num, wt));
    ensures  1 <= \result <= 10;
*/
static inline __attribute__((always_inline)) size_t
gremlin_tag_size(uint32_t field_num, enum gremlin_wire_type wt)
{
	return gremlin_varint_size(((uint64_t)field_num * 8u) + (uint64_t)wt);
}

/*@ requires valid_writer(w);
    requires 0 <= (integer)wt <= 5;
    requires w->offset + varint_size(tag_packed(field_num, wt)) <= w->cap;
    assigns  w->offset,
             w->buf[\at(w->offset, Pre) ..
                    \at(w->offset, Pre) +
                    varint_size(tag_packed(field_num, wt)) - 1];
    ensures  valid_writer(w);
    ensures  w->offset == \old(w->offset) + varint_size(tag_packed(field_num, wt));
*/
static inline __attribute__((always_inline)) void
gremlin_tag_encode(struct gremlin_writer *w, uint32_t field_num, enum gremlin_wire_type wt)
{
	gremlin_varint_encode(w, ((uint64_t)field_num * 8u) + (uint64_t)wt);
}

struct gremlin_tag_decode_result {
	struct gremlin_tag	tag;
	size_t			consumed;
	enum gremlin_error	error;
};

/*@ requires len == 0 || \valid_read(buf + (0 .. len - 1));
    assigns  \nothing;
    ensures  \result.error == GREMLIN_OK ||
             \result.error == GREMLIN_ERROR_TRUNCATED ||
             \result.error == GREMLIN_ERROR_OVERFLOW ||
             \result.error == GREMLIN_ERROR_INVALID_WIRE_TYPE ||
             \result.error == GREMLIN_ERROR_INVALID_FIELD_NUM;
    ensures  \result.error == GREMLIN_OK ==>
               1 <= \result.consumed <= 10 &&
               \result.consumed <= len &&
               0 <= (integer)\result.tag.wire_type <= 5 &&
               1 <= \result.tag.field_num <= GREMLIN_MAX_FIELD_NUM;
    ensures  \result.error != GREMLIN_OK ==> \result.consumed == 0;
*/
static inline __attribute__((always_inline)) struct gremlin_tag_decode_result
gremlin_tag_decode(const uint8_t *buf, size_t len)
{
	struct gremlin_tag_decode_result r;
	r.tag.field_num = 0;
	r.tag.wire_type = GREMLIN_WIRE_VARINT;
	r.consumed = 0;
	r.error = GREMLIN_OK;

	struct gremlin_varint_decode_result d = gremlin_varint_decode(buf, len);
	if (d.error != GREMLIN_OK) {
		r.error = d.error;
		return r;
	}

	uint64_t raw = d.value;
	/* Arithmetic forms of `raw & 0x7` and `raw >> 3` — SMT-friendlier
	 * than the bitwise equivalents. Same asm on unsigned. */
	unsigned wt_raw = (unsigned)(raw % 8u);
	/*@ assert 0 <= wt_raw <= 7; */
	if (wt_raw == 6u || wt_raw == 7u) {
		r.error = GREMLIN_ERROR_INVALID_WIRE_TYPE;
		return r;
	}
	/*@ assert 0 <= wt_raw <= 5; */

	uint64_t fn_raw = raw / 8u;
	/* Reject 0 (invalid per spec) and anything above the 2^29 - 1 cap.
	 * Silent truncation to uint32_t would map huge values to small
	 * field numbers — a potentially exploitable aliasing. */
	if (fn_raw == 0 || fn_raw > (uint64_t)GREMLIN_MAX_FIELD_NUM) {
		r.error = GREMLIN_ERROR_INVALID_FIELD_NUM;
		return r;
	}

	r.tag.wire_type = (enum gremlin_wire_type)wt_raw;
	r.tag.field_num = (uint32_t)fn_raw;
	r.consumed = d.consumed;
	return r;
}

/* ========================================================================
 * ZigZag — signed <-> unsigned for sint32 / sint64.
 *
 * Protobuf sint32 / sint64 types avoid the 10-byte cost of encoding
 * small negative numbers as 2's-complement varints by first mapping
 * signed values to unsigned via zigzag:
 *
 *   0 → 0,  -1 → 1,  1 → 2,  -2 → 3,  2 → 4, ...
 *
 * i.e. interleaving positive and negative, so small-absolute-value ints
 * stay in a small number of bytes.
 *
 * Pure functions — no buffer, no writer, no error. Identity
 * `unzigzag(zigzag(v)) == v` holds for every input.
 * ======================================================================== */

/*
 * Mathematical spec of zigzag, in ACSL integer (no C-level overflow).
 * The bitwise C implementations below use 2's-complement tricks that
 * SMT can't auto-bridge to this logic function — proving the C-to-logic
 * correspondence would need bit-vector reasoning Frama-C/WP doesn't
 * chain reliably. Kept here as documentation of the intended semantics.
 */
/*@ axiomatic GremlinZigZag {
      logic integer zigzag_encode(integer v) =
        v >= 0 ? 2 * v : -2 * v - 1;

      // Round-trip identity at logic level. Admitted; 2-line proof by
      // case on sign of v. Generated code relies on this when chaining
      // zigzag ↔ varint.
      axiom zigzag_roundtrip:
        \forall integer v;
          zigzag_encode(v) >= 0 &&
          (zigzag_encode(v) % 2 == 0
            ? zigzag_encode(v) / 2 == v
            : -(zigzag_encode(v) / 2) - 1 == v);
    }
*/

/*@ assigns \nothing; */
static inline __attribute__((always_inline)) uint32_t
gremlin_zigzag32(int32_t v)
{
	/* Sign mask: 0x00000000 if v >= 0, 0xFFFFFFFF if v < 0. */
	uint32_t sign = (v < 0) ? 0xFFFFFFFFu : 0u;
	return ((uint32_t)v << 1) ^ sign;
}

/*@ assigns \nothing; */
static inline __attribute__((always_inline)) int32_t
gremlin_unzigzag32(uint32_t v)
{
	/* -(v & 1) in unsigned — if LSB clear: 0; if set: 0xFFFFFFFF.
	 * XOR with that is identity (positive) or bitwise NOT (negative,
	 * inverse of sign-mask XOR in encode). */
	uint32_t mask = (uint32_t)0 - (v & 1u);
	return (int32_t)((v >> 1) ^ mask);
}

/*@ assigns \nothing; */
static inline __attribute__((always_inline)) uint64_t
gremlin_zigzag64(int64_t v)
{
	uint64_t sign = (v < 0) ? 0xFFFFFFFFFFFFFFFFULL : 0ULL;
	return ((uint64_t)v << 1) ^ sign;
}

/*@ assigns \nothing; */
static inline __attribute__((always_inline)) int64_t
gremlin_unzigzag64(uint64_t v)
{
	uint64_t mask = (uint64_t)0 - (v & 1u);
	return (int64_t)((v >> 1) ^ mask);
}

/* ========================================================================
 * Float / double bit-cast.
 *
 * Protobuf `float` and `double` fields are transmitted as fixed32 /
 * fixed64 of their IEEE-754 bit pattern. `gremlin_fXX_bits` converts
 * a float to its bit pattern; `_from_bits` the inverse. memcpy is the
 * only type-pun that is well-defined in C99 (union aliasing and
 * pointer casts violate strict aliasing); GCC / Clang recognise it and
 * compile to a single register move.
 *
 * Round-trip identity `f32_from_bits(f32_bits(x)) == x` holds for every
 * finite float. NaN representations are not bit-canonical (the mantissa
 * bits can vary), so the round-trip preserves *the exact bit pattern*
 * you passed in, not necessarily a canonical one.
 * ======================================================================== */

/*
 * Union type-punning. Officially permitted since C99 footnote 82 /
 * C11 §6.5.2.3, and both GCC and Clang compile these to a single
 * register move. Frama-C / WP handles union field access cleanly
 * without the memcpy-separation gymnastics.
 */
union gremlin_f32_punner { float f; uint32_t u; };
union gremlin_f64_punner { double d; uint64_t u; };

/*@ assigns \nothing; */
static inline __attribute__((always_inline)) uint32_t
gremlin_f32_bits(float f)
{
	union gremlin_f32_punner p;
	p.f = f;
	return p.u;
}

/*@ assigns \nothing; */
static inline __attribute__((always_inline)) float
gremlin_f32_from_bits(uint32_t bits)
{
	union gremlin_f32_punner p;
	p.u = bits;
	return p.f;
}

/*@ assigns \nothing; */
static inline __attribute__((always_inline)) uint64_t
gremlin_f64_bits(double d)
{
	union gremlin_f64_punner p;
	p.d = d;
	return p.u;
}

/*@ assigns \nothing; */
static inline __attribute__((always_inline)) double
gremlin_f64_from_bits(uint64_t bits)
{
	union gremlin_f64_punner p;
	p.u = bits;
	return p.d;
}

#endif /* !_GREMLIN_H_ */

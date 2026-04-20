#include <stdio.h>
#include <string.h>

#include "gremlinc/naming.h"
#include "gremlind/nodes.h"

static const char *const C_KEYWORDS[] = {
	/* C11 §6.4.1 reserved words — 44 total. */
	"auto", "break", "case", "char", "const", "continue", "default",
	"do", "double", "else", "enum", "extern", "float", "for", "goto",
	"if", "inline", "int", "long", "register", "restrict", "return",
	"short", "signed", "sizeof", "static", "struct", "switch",
	"typedef", "union", "unsigned", "void", "volatile", "while",
	"_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
	"_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local"
};
#define N_C_KEYWORDS (sizeof(C_KEYWORDS) / sizeof(C_KEYWORDS[0]))

bool
gremlinc_is_c_keyword(const char *name)
{
	if (name == NULL) return false;
	for (size_t i = 0; i < N_C_KEYWORDS; i++) {
		if (strcmp(C_KEYWORDS[i], name) == 0) return true;
	}
	return false;
}

/*@ predicate is_lower_or_digit(char c) =
      ('a' <= c <= 'z') || ('0' <= c <= '9');
    predicate is_upper(char c) =
      'A' <= c <= 'Z';
    predicate is_lower(char c) =
      'a' <= c <= 'z';
 */

/*@ assigns \nothing;
    ensures \result != 0 <==> is_upper(c);
*/
static inline int
is_upper_c(char c) { return c >= 'A' && c <= 'Z'; }

/*@ assigns \nothing;
    ensures \result != 0 <==> is_lower(c);
*/
static inline int
is_lower_c(char c) { return c >= 'a' && c <= 'z'; }

/*@ assigns \nothing;
    ensures \result != 0 <==> is_lower_or_digit(c);
*/
static inline int
is_lower_or_digit_c(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

/*@ requires valid_arena(arena);
    requires span_len == 0 || \valid_read(span + (0 .. span_len - 1));
    requires span_len <= 0x7fffffffffffffff / 2 - 1;
    requires out_len == \null || \valid(out_len);
    ensures  \result != \null ==> \valid(\result + (0 .. 2 * span_len));
*/
const char *
gremlinc_to_snake_case(struct gremlind_arena *arena,
		       const char *span, size_t span_len,
		       size_t *out_len)
{
	/* Every source char maps to either `c` or `_c` — so 2*N is a tight
	 * worst-case upper bound. +1 keeps room for the NUL terminator. */
	size_t cap = span_len * 2 + 1;
	char *out = gremlind_arena_alloc(arena, cap);
	if (out == NULL) return NULL;

	size_t o = 0;
	/*@ loop invariant 0 <= i <= span_len;
	    loop invariant 0 <= o <= 2 * i;
	    loop invariant o < cap;
	    loop assigns i, o, out[0 .. cap - 2];
	    loop variant span_len - i;
	*/
	for (size_t i = 0; i < span_len; i++) {
		char c = span[i];
		int insert_underscore = 0;
		if (i > 0) {
			char prev = span[i - 1];
			/* lower-or-digit → upper */
			if (is_lower_or_digit_c(prev) && is_upper_c(c)) {
				insert_underscore = 1;
			}
			/* upper → upper followed by lower (acronym edge) */
			else if (is_upper_c(prev) && is_upper_c(c)
				 && i + 1 < span_len
				 && is_lower_c(span[i + 1])) {
				insert_underscore = 1;
			}
		}
		if (insert_underscore) {
			/*@ assert o + 1 < cap; */
			out[o++] = '_';
		}
		/*@ assert o < cap; */
		if (is_upper_c(c)) {
			out[o++] = (char)(c + ('a' - 'A'));
		} else {
			out[o++] = c;
		}
	}
	/*@ assert o < cap; */
	out[o] = '\0';
	if (out_len != NULL) *out_len = o;
	return out;
}

void
gremlinc_name_scope_init(struct gremlinc_name_scope *s, struct gremlind_arena *arena)
{
	s->arena = arena;
	s->head = NULL;
	s->count = 0;
}

bool
gremlinc_name_scope_has(const struct gremlinc_name_scope *s, const char *name)
{
	if (s == NULL || name == NULL) return false;
	for (struct gremlinc_name_entry *e = s->head; e != NULL; e = e->next) {
		if (strcmp(e->name, name) == 0) return true;
	}
	return false;
}

static enum gremlinp_parsing_error
scope_register(struct gremlinc_name_scope *s, const char *name)
{
	struct gremlinc_name_entry *e = gremlind_arena_alloc(s->arena, sizeof(*e));
	if (e == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
	e->name = name;
	e->next = s->head;
	s->head = e;
	s->count++;
	return GREMLINP_OK;
}

/*
 * Duplicate `src[0..len-1]` into the arena as a NUL-terminated cstring,
 * optionally with a suffix. pass suffix=NULL / suffix_len=0 for a plain copy.
 */
static const char *
arena_dup_with_suffix(struct gremlind_arena *arena,
		      const char *src, size_t len,
		      const char *suffix, size_t suffix_len)
{
	size_t total = len + suffix_len + 1;
	char *out = gremlind_arena_alloc(arena, total);
	if (out == NULL) return NULL;
	memcpy(out, src, len);
	if (suffix_len > 0) memcpy(out + len, suffix, suffix_len);
	out[len + suffix_len] = '\0';
	return out;
}

const char *
gremlinc_name_scope_mangle(struct gremlinc_name_scope *s,
			   const char *raw, size_t raw_len)
{
	if (s == NULL || raw == NULL || raw_len == 0) return NULL;

	/* Candidate starts as raw; keyword check upgrades to raw_ */
	const char *candidate = arena_dup_with_suffix(s->arena, raw, raw_len, NULL, 0);
	if (candidate == NULL) return NULL;

	if (gremlinc_is_c_keyword(candidate)) {
		candidate = arena_dup_with_suffix(s->arena, raw, raw_len, "_", 1);
		if (candidate == NULL) return NULL;
	}

	if (!gremlinc_name_scope_has(s, candidate)) {
		if (scope_register(s, candidate) != GREMLINP_OK) return NULL;
		return candidate;
	}

	/* Collision: try candidate_1, _2, _3, ... */
	char suffix[16];
	for (unsigned n = 1; n < 1000000u; n++) {
		int sn = snprintf(suffix, sizeof suffix, "_%u", n);
		if (sn <= 0 || sn >= (int)sizeof suffix) return NULL;
		const char *try = arena_dup_with_suffix(s->arena,
			candidate, strlen(candidate), suffix, (size_t)sn);
		if (try == NULL) return NULL;
		if (!gremlinc_name_scope_has(s, try)) {
			if (scope_register(s, try) != GREMLINP_OK) return NULL;
			return try;
		}
	}
	return NULL;	/* absurdly saturated — a million collisions */
}

const char *
gremlinc_cname_for_type(struct gremlinc_name_scope *s,
			const struct gremlind_scoped_name *sn)
{
	if (s == NULL || sn == NULL || sn->n_segments == 0) return NULL;

	/* Join into a scratch buffer in the arena first. Size = sum + seps + NUL. */
	size_t total = 0;
	for (size_t i = 0; i < sn->n_segments; i++) total += sn->segments[i].length;
	if (sn->n_segments > 1) total += (sn->n_segments - 1);

	char *raw = gremlind_arena_alloc(s->arena, total + 1);
	if (raw == NULL) return NULL;

	size_t off = 0;
	for (size_t i = 0; i < sn->n_segments; i++) {
		if (i > 0) raw[off++] = '_';
		memcpy(raw + off, sn->segments[i].start, sn->segments[i].length);
		off += sn->segments[i].length;
	}
	raw[off] = '\0';

	return gremlinc_name_scope_mangle(s, raw, off);
}

enum gremlinp_parsing_error
gremlinc_assign_c_names(struct gremlinc_name_scope *s, struct gremlind_file *file)
{
	if (s == NULL || file == NULL) return GREMLINP_ERROR_NULL_POINTER;

	/* Order: enums first, then messages. Both share the same typedef
	 * namespace; the order only affects which one gets a `_N` suffix
	 * on a flat-join collision. Emitting enums ahead of messages
	 * matches the current codegen output order and keeps diagnostics
	 * stable when collisions happen. */
	for (size_t i = 0; i < file->enums.count; i++) {
		struct gremlind_enum *e = &file->enums.items[i];
		e->c_name = gremlinc_cname_for_type(s, &e->scoped_name);
		if (e->c_name == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
	}
	for (size_t i = 0; i < file->messages.count; i++) {
		struct gremlind_message *m = &file->messages.items[i];
		m->c_name = gremlinc_cname_for_type(s, &m->scoped_name);
		if (m->c_name == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
	}
	return GREMLINP_OK;
}

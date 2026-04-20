#ifndef _GREMLINC_NAMING_H_
#define _GREMLINC_NAMING_H_

#include <stdbool.h>
#include <stddef.h>

#include "gremlind/name.h"
#include "gremlind/arena.h"
#include "gremlinp/errors.h"

/*
 * Identifier mangling + usage tracking for C codegen.
 *
 * Rules:
 *
 *  1. Protobuf identifier chars are a subset of C's — no character
 *     escaping needed. Segments are copied verbatim (no case
 *     conversion).
 *
 *  2. Typedef names are built from a descriptor's scoped_name by
 *     joining segments with `_`. e.g. scoped `foo.bar.A.B` → `foo_bar_A_B`.
 *
 *  3. C11 keyword collisions: if the resulting identifier exactly
 *     matches a C keyword (`int`, `struct`, `return`, ...), append a
 *     single `_`. `int` → `int_`.
 *
 *  4. Flat-join collisions: two different proto types can produce the
 *     same post-join identifier. Example:
 *        `message A_B { message C {} }`  → scoped ["A_B", "C"]   → `A_B_C`
 *        `message A { message B_C {} }`  → scoped ["A", "B_C"]   → `A_B_C`
 *     Both valid and distinct in proto, both flatten to the same C
 *     name. The name scope tracks emitted names; the second occurrence
 *     gets a suffix `_1`, `_2`, ... chosen as the smallest integer
 *     that makes the result unique.
 *
 *  5. Namespaces: C typedefs, enum value names, and functions all live
 *     in the global ordinary-identifier space — this module tracks
 *     them as one scope. Struct members are per-struct and handled by
 *     a separate scope in message codegen.
 *
 * Not handled (documented, deferred):
 *  - Reserved `__X` and `_Capital` prefixes (implementation-reserved;
 *    proto names rarely hit these).
 *  - Case-conversion policies (snake_case, CamelCase, etc).
 *  - Cross-file namespace coordination.
 */

/* Linked-list node of emitted names. Backing storage is the arena. */
struct gremlinc_name_entry {
	const char			*name;
	struct gremlinc_name_entry	*next;
};

struct gremlinc_name_scope {
	struct gremlind_arena		*arena;
	struct gremlinc_name_entry	*head;
	size_t				 count;
};

void					gremlinc_name_scope_init(struct gremlinc_name_scope *s,
								 struct gremlind_arena *arena);

/* True if `name` is already registered in the scope. */
bool					gremlinc_name_scope_has(const struct gremlinc_name_scope *s,
								const char *name);

/*
 * Pick a unique C-safe identifier for a raw (already-joined) candidate.
 * Steps:
 *   - If `raw` is a C keyword, try `raw_` first.
 *   - If the result is already in the scope, try `_1`, `_2`, ... until unique.
 *   - Register the chosen name and return it (arena-allocated NUL-terminated).
 *
 * Returns NULL on arena OOM. `raw` does not need to be NUL-terminated;
 * pass its length.
 */
const char *				gremlinc_name_scope_mangle(struct gremlinc_name_scope *s,
								   const char *raw, size_t raw_len);

/*
 * Build a C typedef name from a scoped_name by joining segments with `_`
 * and passing through the scope's mangler (keyword + collision handling).
 * Returns NULL on arena OOM.
 */
const char *				gremlinc_cname_for_type(struct gremlinc_name_scope *s,
								const struct gremlind_scoped_name *sn);

/* True if `name` is a C11 keyword (44 total). */
bool					gremlinc_is_c_keyword(const char *name);

/*
 * Convert a camelCase / PascalCase span to snake_case, arena-allocated.
 *
 * Rules:
 *   1. `[a-z0-9] → [A-Z]` inserts `_` before the upper.
 *         `isUsernameDeleted` → `is_Username_Deleted`
 *   2. `[A-Z][A-Z][a-z]` inserts `_` before the trailing upper
 *      (acronym boundary).
 *         `APIKey`            → `API_Key`
 * After the passes, all `[A-Z]` are lowercased.
 *
 * Inputs already in snake_case, plain lowercase, or pure numeric pass
 * through unchanged. Output is NUL-terminated; `*out_len` is set to
 * strlen. Returns NULL on arena OOM.
 *
 * Upper bound on output length is `2 * span_len`, attained by an input
 * where every character is a transition point.
 */
const char *				gremlinc_to_snake_case(struct gremlind_arena *arena,
								       const char *span, size_t span_len,
								       size_t *out_len);

/*
 * Codegen pre-pass: walks every enum and message in `file` and claims
 * each type's C typedef name in the shared scope, storing the result on
 * the AST node's `c_name` field. Must be called before any emit_* so
 * that a message field referencing another type can look up the
 * target's C name instead of re-computing it (which would double-claim
 * the scope and get a `_1` collision suffix).
 *
 * Call once per file, across all files, before emission begins. Errors
 * propagate GREMLINP_ERROR_OUT_OF_MEMORY from the mangler.
 */
struct gremlind_file;	/* fwd — declared in gremlind/nodes.h */
enum gremlinp_parsing_error		gremlinc_assign_c_names(struct gremlinc_name_scope *s,
								struct gremlind_file *file);

#endif /* !_GREMLINC_NAMING_H_ */

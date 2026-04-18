#ifndef _GREMLIND_STD_H_
#define _GREMLIND_STD_H_

#include <stddef.h>

#include "axioms.h"

/*
 * Thin libc-backed helpers for the chunked arena. Same trick as the
 * parser's std.h: these are declared extern here with strong ACSL
 * contracts, defined in src/arena_std.c, and intentionally NOT passed to
 * Frama-C WP — the verifier sees only the declarations + contracts. This
 * keeps the arena core verification libc-free while still providing a
 * turn-key malloc-backed allocator for callers who don't want to roll
 * their own pool.
 */

/*
 * Allocate a chunk of at least `min_size` bytes backed by malloc. On
 * success stores the actual capacity in *out_cap and returns a pointer to
 * writable, 8-aligned bytes. On failure returns NULL. Compatible with
 * gremlind_grow_fn.
 */
/*@ requires \valid(out_cap);
    requires min_size > 0;
    requires min_size <= 0x7FFFFFFE;
    assigns  *out_cap;
    ensures  \result == \null ||
             (\valid((char *)\result + (0 .. *out_cap - 1)) &&
              *out_cap >= min_size &&
              *out_cap <= 0x7FFFFFFE);
*/
char		*gremlind_malloc_grow(size_t min_size, size_t *out_cap, void *ctx);

/*
 * Initialize an arena backed by malloc: allocates the first chunk of
 * `first_cap` bytes and wires gremlind_malloc_grow as the growth
 * callback. `ctx` is passed through to gremlind_malloc_grow (unused by
 * the default implementation).
 *
 * Returns true on success, false if the initial malloc failed.
 */
bool		gremlind_arena_init_malloc(struct gremlind_arena *a, size_t first_cap);

/*
 * Free every chunk that was malloc'd into the arena (i.e. every chunk
 * whose base was returned by gremlind_malloc_grow or the matching
 * first-chunk allocation). Does not touch chunks pushed in via
 * gremlind_arena_push_chunk directly — those stay the caller's
 * responsibility.
 */
void		gremlind_arena_free_malloc(struct gremlind_arena *a);

#endif /* !_GREMLIND_STD_H_ */

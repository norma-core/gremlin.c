#ifndef _GREMLIND_ARENA_H_
#define _GREMLIND_ARENA_H_

#include <stdbool.h>
#include <stddef.h>

#include "axioms.h"

/*
 * Chunked bump-pointer arena.
 *
 * Usage model:
 *   1. Seed the arena with a first chunk at init time. Either provide it
 *      yourself (gremlind_arena_init) or let arena_std.h hand it to you
 *      via malloc (gremlind_arena_init_malloc).
 *   2. Hand the arena to AST builders. They call gremlind_arena_alloc
 *      directly and never think about chunk boundaries — if the current
 *      chunk fills, the arena calls its stored grow callback to get a new
 *      chunk and transparently bumps from there.
 *   3. When done, free the chunks (arena_std.h's gremlind_arena_free walks
 *      and frees every chunk it owns).
 *
 * Stability: chunks never move once pushed, so every pointer the arena
 * hands out stays valid until the arena is freed.
 *
 * Alignment: allocations round up to GREMLIND_ALIGN (8). Chunk bases must
 * be 8-aligned. malloc satisfies this on every sane libc.
 */

#define GREMLIND_ALIGN 8u

/*
 * Initialize an arena with a caller-provided first chunk and an optional
 * growth callback. Pass grow == NULL if the arena must stay strictly
 * within the provided chunk (allocations beyond it return NULL).
 */
void					gremlind_arena_init(struct gremlind_arena *a,
							    char *base, size_t cap,
							    gremlind_grow_fn grow, void *grow_ctx);

/*
 * Append a new chunk to the arena. Returns true on success, false when
 * the arena already has GREMLIND_MAX_CHUNKS chunks. Typically only called
 * by gremlind_arena_alloc's auto-grow path — callers rarely need it
 * directly.
 */
bool					gremlind_arena_push_chunk(struct gremlind_arena *a,
								  char *base, size_t cap);

/*
 * Bump-allocate `size` bytes. Rounds up to GREMLIND_ALIGN. If the current
 * chunk is full, calls the stored grow callback to append a new chunk and
 * retries. Returns NULL only on genuine out-of-memory (grow returned
 * NULL), when the arena already has GREMLIND_MAX_CHUNKS chunks and is
 * full, or when `size` is 0 / unreasonably large.
 *
 * The ACSL contract below is admitted — the body calls an opaque
 * function pointer (the grow callback) which WP can't reason about
 * without per-call contracts. Callers (e.g. build.c) verify against
 * this contract; the body is tested at runtime only. An audit only
 * needs to check that this contract is faithful to the implementation
 * in src/arena.c.
 */
/*@ requires valid_arena(a);
    assigns  *a;
    ensures  valid_arena(a);
    ensures  a->n_chunks >= \old(a->n_chunks);
    ensures  \result != \null ==>
               size > 0 &&
               \valid((char *)\result + (0 .. size - 1));
*/
void					*gremlind_arena_alloc(struct gremlind_arena *a, size_t size);

/*
 * Bump-allocate within the current chunk only, without ever calling the
 * grow callback. Returns NULL when the current chunk cannot fit `size`
 * bytes. This is the verified primitive; gremlind_arena_alloc wraps it
 * with the auto-grow retry.
 */
void					*gremlind_arena_try_alloc(struct gremlind_arena *a, size_t size);

/*
 * Total bytes currently used across all chunks (post-alignment padding
 * included). Useful for telemetry and tests.
 */
size_t					gremlind_arena_bytes_used(const struct gremlind_arena *a);

#endif /* !_GREMLIND_ARENA_H_ */

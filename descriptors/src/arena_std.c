#include <stdlib.h>

#include "gremlind/arena.h"
#include "gremlind/std.h"

/*
 * Unverified convenience layer over the arena core. Same pattern as the
 * parser's src/entries/std.c — intentionally NOT passed to Frama-C WP; the
 * verifier sees only the declarations in gremlind/std.h + their
 * contracts.
 */

char *
gremlind_malloc_grow(size_t min_size, size_t *out_cap, void *ctx)
{
	(void)ctx;

	if (min_size == 0 || min_size > (size_t)0x7FFFFFFE) {
		return (char *)0;
	}

	/* Round up to the nearest 4 KiB so small allocations don't thrash. */
	const size_t PAGE = 4096;
	size_t cap = (min_size + PAGE - 1) & ~(size_t)(PAGE - 1);
	if (cap < min_size || cap > (size_t)0x7FFFFFFE) {
		cap = min_size;
	}

	void *mem = malloc(cap);
	if (mem == NULL) {
		return (char *)0;
	}

	*out_cap = cap;
	return (char *)mem;
}

bool
gremlind_arena_init_malloc(struct gremlind_arena *a, size_t first_cap)
{
	if (first_cap == 0 || first_cap > (size_t)0x7FFFFFFE) {
		return false;
	}

	void *mem = malloc(first_cap);
	if (mem == NULL) {
		return false;
	}

	gremlind_arena_init(a, (char *)mem, first_cap, gremlind_malloc_grow, NULL);
	return true;
}

void
gremlind_arena_free_malloc(struct gremlind_arena *a)
{
	for (size_t i = 0; i < a->n_chunks; i++) {
		free(a->chunks[i].base);
		a->chunks[i].base = NULL;
		a->chunks[i].cap = 0;
		a->chunks[i].used = 0;
	}
	a->n_chunks = 0;
	a->grow = NULL;
	a->grow_ctx = NULL;
}

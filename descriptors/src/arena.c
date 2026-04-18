#include "gremlind/arena.h"
#include "gremlind/axioms.h"

/*@ requires \valid(a);
    requires cap > 0;
    requires cap <= 0x7FFFFFFE;
    requires \valid(base + (0 .. cap - 1));
    assigns  *a;
    ensures  valid_arena(a);
    ensures  a->n_chunks == 1;
    ensures  a->chunks[0].base == base;
    ensures  a->chunks[0].cap == cap;
    ensures  a->chunks[0].used == 0;
    ensures  a->grow == grow;
    ensures  a->grow_ctx == grow_ctx;
*/
void
gremlind_arena_init(struct gremlind_arena *a, char *base, size_t cap,
		    gremlind_grow_fn grow, void *grow_ctx)
{
	a->n_chunks = 1;
	a->chunks[0].base = base;
	a->chunks[0].cap = cap;
	a->chunks[0].used = 0;
	a->grow = grow;
	a->grow_ctx = grow_ctx;

	/*@ loop invariant 1 <= i <= GREMLIND_MAX_CHUNKS;
	    loop assigns i, a->chunks[1 .. GREMLIND_MAX_CHUNKS - 1];
	    loop variant GREMLIND_MAX_CHUNKS - i;
	*/
	for (size_t i = 1; i < GREMLIND_MAX_CHUNKS; i++) {
		a->chunks[i].base = (char *)0;
		a->chunks[i].cap = 0;
		a->chunks[i].used = 0;
	}
}

/*@ requires valid_arena(a);
    requires cap > 0;
    requires cap <= 0x7FFFFFFE;
    requires \valid(base + (0 .. cap - 1));
    assigns  a->n_chunks, a->chunks[a->n_chunks];
    ensures  \result == \true ==>
               a->n_chunks == \old(a->n_chunks) + 1 &&
               a->n_chunks <= GREMLIND_MAX_CHUNKS &&
               a->chunks[a->n_chunks - 1].base == base &&
               a->chunks[a->n_chunks - 1].cap == cap &&
               a->chunks[a->n_chunks - 1].used == 0;
    ensures  \result == \false ==>
               a->n_chunks == \old(a->n_chunks);
    ensures  valid_arena(a);
*/
bool
gremlind_arena_push_chunk(struct gremlind_arena *a, char *base, size_t cap)
{
	if (a->n_chunks >= GREMLIND_MAX_CHUNKS) {
		return false;
	}

	a->chunks[a->n_chunks].base = base;
	a->chunks[a->n_chunks].cap = cap;
	a->chunks[a->n_chunks].used = 0;
	a->n_chunks++;

	return true;
}

/*@ requires valid_arena(a);
    assigns  a->chunks[a->n_chunks - 1].used;
    ensures  valid_arena(a);
    ensures  a->n_chunks == \old(a->n_chunks);
    ensures  \result == \null ||
             \valid((char *)\result + (0 .. size - 1));
    ensures  a->chunks[a->n_chunks - 1].used >=
               \old(a->chunks[a->n_chunks - 1].used);
*/
void *
gremlind_arena_try_alloc(struct gremlind_arena *a, size_t size)
{
	if (size == 0) {
		return (void *)0;
	}

	if (size > (size_t)0x7FFFFFFE - (GREMLIND_ALIGN - 1)) {
		return (void *)0;
	}
	size_t aligned_size = size;
	size_t rem = size % GREMLIND_ALIGN;
	if (rem != 0) {
		aligned_size = size + (GREMLIND_ALIGN - rem);
	}
	/*@ assert aligned_size >= size; */
	/*@ assert aligned_size <= size + (GREMLIND_ALIGN - 1); */

	size_t idx = a->n_chunks - 1;
	/*@ assert 0 <= idx < a->n_chunks; */
	struct gremlind_arena_chunk *cur = &a->chunks[idx];

	/*@ assert cur->used <= cur->cap; */
	if (aligned_size > cur->cap - cur->used) {
		return (void *)0;
	}

	/*@ assert cur->used + aligned_size <= cur->cap; */
	/*@ assert size >= 1; */
	char *p = cur->base + cur->used;
	/*@ assert \valid(p + (0 .. size - 1)); */
	cur->used += aligned_size;

	return (void *)p;
}

/*
 * Auto-growing allocator. Not in the verified set — it calls the stored
 * grow callback, which is opaque to WP. The verified primitive is
 * gremlind_arena_try_alloc above; the build layer may call either. When
 * an AST builder calls gremlind_arena_alloc, it gets auto-growth on chunk
 * exhaustion and only sees NULL on genuine OOM (grow returned NULL or the
 * chunk array is full).
 */
void *
gremlind_arena_alloc(struct gremlind_arena *a, size_t size)
{
	void *p = gremlind_arena_try_alloc(a, size);
	if (p != (void *)0) {
		return p;
	}

	if (size == 0 || a->grow == (gremlind_grow_fn)0) {
		return (void *)0;
	}
	if (a->n_chunks >= GREMLIND_MAX_CHUNKS) {
		return (void *)0;
	}

	/* Geometric growth: ask for at least the current total capacity (so
	 * chunks roughly double in size), or the individual alloc's size if
	 * larger. Keeps the chunk-array depth O(log total_bytes) — 64 chunks
	 * comfortably covers multi-gigabyte arenas. */
	size_t total_cap = 0;
	for (size_t i = 0; i < a->n_chunks; i++) {
		total_cap += a->chunks[i].cap;
	}
	size_t requested = size > total_cap ? size : total_cap;

	size_t new_cap = 0;
	char *mem = a->grow(requested, &new_cap, a->grow_ctx);
	if (mem == (char *)0 || new_cap < size || new_cap > (size_t)0x7FFFFFFE) {
		return (void *)0;
	}

	if (!gremlind_arena_push_chunk(a, mem, new_cap)) {
		return (void *)0;
	}

	return gremlind_arena_try_alloc(a, size);
}

/*@ requires valid_arena(a);
    assigns  \nothing;
    ensures  \result >= 0;
*/
size_t
gremlind_arena_bytes_used(const struct gremlind_arena *a)
{
	size_t total = 0;

	/*@ loop invariant 0 <= k <= a->n_chunks;
	    loop invariant total >= 0;
	    loop assigns k, total;
	    loop variant a->n_chunks - k;
	*/
	for (size_t k = 0; k < a->n_chunks; k++) {
		total += a->chunks[k].used;
	}

	return total;
}

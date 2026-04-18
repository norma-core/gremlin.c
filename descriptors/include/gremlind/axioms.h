#ifndef _GREMLIND_AXIOMS_H_
#define _GREMLIND_AXIOMS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
 * gremlind — formally verified protobuf AST layer.
 *
 * This file is the trust base. Everything declared `axiom` here is admitted
 * without proof; predicates used by WP live here too. An audit only needs
 * to read this file to know what is being trusted.
 */

/* ------------------------------------------------------------------------
 * Arena
 * ------------------------------------------------------------------------ */

/*
 * A chunk is a contiguous caller-provided region plus a bump pointer. Chunks
 * never move once added to an arena, so pointers handed out by the arena
 * stay stable for the arena's lifetime.
 */
struct gremlind_arena_chunk {
	char				*base;
	size_t				cap;
	size_t				used;
};

/*
 * Caller-provided growth callback. Asked for a chunk of at least `min_size`
 * bytes; must return a pointer to that many writable, 8-aligned bytes and
 * store the actual capacity in *out_cap, or return NULL on failure. The
 * returned region's lifetime must outlive the arena.
 *
 * WP sees this as an opaque function pointer with the contract declared on
 * the extern wrapper gremlind_arena_invoke_grow in std.h.
 */
typedef char *(*gremlind_grow_fn)(size_t min_size, size_t *out_cap, void *ctx);

/*
 * Bounded inline array of chunk descriptors. Keeping it bounded and inline
 * keeps WP away from recursive linked-list reasoning.
 */
#define GREMLIND_MAX_CHUNKS 64

struct gremlind_arena {
	struct gremlind_arena_chunk	chunks[GREMLIND_MAX_CHUNKS];
	size_t				n_chunks;
	gremlind_grow_fn		grow;
	void				*grow_ctx;
};

/*@ predicate valid_chunk(struct gremlind_arena_chunk *c) =
      \valid_read(c) &&
      c->used <= c->cap &&
      c->cap <= 0x7FFFFFFE &&
      (c->cap > 0 ==> \valid(c->base + (0 .. c->cap - 1)));

    predicate valid_arena(struct gremlind_arena *a) =
      \valid(a) &&
      1 <= a->n_chunks <= GREMLIND_MAX_CHUNKS &&
      (\forall integer k; 0 <= k < a->n_chunks ==>
        a->chunks[k].used <= a->chunks[k].cap &&
        a->chunks[k].cap <= 0x7FFFFFFE &&
        (a->chunks[k].cap > 0 ==>
          \valid(a->chunks[k].base + (0 .. a->chunks[k].cap - 1))));
 */

#endif /* !_GREMLIND_AXIOMS_H_ */

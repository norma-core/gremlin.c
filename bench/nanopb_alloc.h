/* Included ahead of every nanopb source: decode allocates from bench_nanopb.c's pool. */
#include <stddef.h>

void	*bench_realloc(void *p, size_t size);
void	 bench_free(void *p);

#define PB_ENABLE_MALLOC 1
#define pb_realloc(p, size) bench_realloc(p, size)
#define pb_free(p) bench_free(p)

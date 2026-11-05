/* Counts heap allocations and prints the total at exit; preloaded by
 * bench/allocs.sh (DYLD_INSERT_LIBRARIES on macOS, LD_PRELOAD with glibc). */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static size_t count;

#ifdef __APPLE__
static void *c_malloc(size_t s) { count++; return malloc(s); }
static void *c_calloc(size_t n, size_t s) { count++; return calloc(n, s); }
static void *c_realloc(void *p, size_t s) { count++; return realloc(p, s); }
static void *c_aligned(size_t a, size_t s) { count++; return aligned_alloc(a, s); }
static int c_memalign(void **p, size_t a, size_t s) { count++; return posix_memalign(p, a, s); }

__attribute__((used, section("__DATA,__interpose"))) static const struct {
	const void *with, *of;
} interpose[] = {
	{ (const void *)c_malloc, (const void *)malloc },
	{ (const void *)c_calloc, (const void *)calloc },
	{ (const void *)c_realloc, (const void *)realloc },
	{ (const void *)c_aligned, (const void *)aligned_alloc },
	{ (const void *)c_memalign, (const void *)posix_memalign },
};
#else
void *__libc_malloc(size_t);
void *__libc_calloc(size_t, size_t);
void *__libc_realloc(void *, size_t);
void *__libc_memalign(size_t, size_t);

void *malloc(size_t s) { count++; return __libc_malloc(s); }
void *calloc(size_t n, size_t s) { count++; return __libc_calloc(n, s); }
void *realloc(void *p, size_t s) { count++; return __libc_realloc(p, s); }
void *memalign(size_t a, size_t s) { count++; return __libc_memalign(a, s); }
void *aligned_alloc(size_t a, size_t s) { count++; return __libc_memalign(a, s); }

int
posix_memalign(void **p, size_t a, size_t s)
{
	count++;
	if (a < sizeof(void *) || (a & (a - 1)) != 0) return EINVAL;
	*p = __libc_memalign(a, s);
	return *p != NULL ? 0 : ENOMEM;
}
#endif

__attribute__((destructor)) static void
report(void)
{
	fprintf(stderr, "allocations %zu\n", count);
}

#ifndef GREMLIN_BENCH_H
#define GREMLIN_BENCH_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline int64_t
bench_now_ns(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (int64_t)t.tv_sec * 1000000000LL + (int64_t)t.tv_nsec;
}

static inline uint8_t *
bench_load(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "cannot open %s\n", path);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *b = (uint8_t *)malloc(n > 0 ? (size_t)n : 1u);
	if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) {
		fprintf(stderr, "cannot read %s\n", path);
		exit(1);
	}
	fclose(f);
	*len = (size_t)n;
	return b;
}

static inline size_t
bench_iterations(int argc, char **argv)
{
	if (argc >= 2) {
		char *end;
		unsigned long long v = strtoull(argv[1], &end, 10);
		if (*end == '\0' && v > 0)
			return (size_t)v;
	}
	return 1000000u;
}

/* One line per measurement, read by bench/run.sh. */
static inline void
bench_report(const char *workload, const char *op, int64_t total_ns, size_t iterations)
{
	printf("%s %s %.1f\n", workload, op, (double)total_ns / (double)iterations);
	fflush(stdout);
}

/* BENCH_ONLY=workload/op runs that measurement alone (bench/allocs.sh). */
static inline int
bench_selected(const char *workload, const char *op)
{
	const char *only = getenv("BENCH_ONLY");
	char want[64];
	if (only == NULL) return 1;
	snprintf(want, sizeof want, "%s/%s", workload, op);
	return strcmp(only, want) == 0;
}

/* Runs body iterations times after 100 to warm up, as gremlin, zig and go do; body sees bench_i. */
#define BENCH(workload, op, iterations, ...)                                     \
	do {                                                                     \
		if (!bench_selected(workload, op)) break;                       \
		size_t bench_n = (iterations);                                   \
		for (size_t bench_i = 0; bench_i < 100; bench_i++) {             \
			__VA_ARGS__;                                             \
		}                                                                \
		int64_t bench_t = bench_now_ns();                                \
		for (size_t bench_i = 0; bench_i < bench_n; bench_i++) {         \
			__VA_ARGS__;                                             \
		}                                                                \
		bench_report(workload, op, bench_now_ns() - bench_t, bench_n);   \
	} while (0)

#endif /* GREMLIN_BENCH_H */

#include "tests.h"
#include "gremlind/lib.h"

#include <stdint.h>
#include <stdlib.h>

static TEST(arena_init_single_chunk)
{
	char buf[1024];
	struct gremlind_arena a;

	gremlind_arena_init(&a, buf, sizeof(buf), NULL, NULL);

	ASSERT_EQ(1, a.n_chunks);
	ASSERT_EQ(sizeof(buf), a.chunks[0].cap);
	ASSERT_EQ(0, a.chunks[0].used);
	ASSERT_EQ(0, gremlind_arena_bytes_used(&a));
}

static TEST(arena_alloc_basic)
{
	char buf[1024];
	struct gremlind_arena a;

	gremlind_arena_init(&a, buf, sizeof(buf), NULL, NULL);

	void *p1 = gremlind_arena_alloc(&a, 16);
	void *p2 = gremlind_arena_alloc(&a, 32);

	ASSERT_NOT_NULL(p1);
	ASSERT_NOT_NULL(p2);
	ASSERT_TRUE((uintptr_t)p1 % GREMLIND_ALIGN == 0);
	ASSERT_TRUE((uintptr_t)p2 % GREMLIND_ALIGN == 0);
	ASSERT_TRUE((char *)p2 > (char *)p1);
	ASSERT_EQ(16 + 32, gremlind_arena_bytes_used(&a));
}

static TEST(arena_alloc_rounds_to_alignment)
{
	char buf[1024];
	struct gremlind_arena a;

	gremlind_arena_init(&a, buf, sizeof(buf), NULL, NULL);

	void *p1 = gremlind_arena_alloc(&a, 1);
	void *p2 = gremlind_arena_alloc(&a, 1);

	ASSERT_NOT_NULL(p1);
	ASSERT_NOT_NULL(p2);
	ASSERT_EQ(GREMLIND_ALIGN, (char *)p2 - (char *)p1);
}

static TEST(arena_alloc_zero_returns_null)
{
	char buf[1024];
	struct gremlind_arena a;

	gremlind_arena_init(&a, buf, sizeof(buf), NULL, NULL);

	ASSERT_NULL(gremlind_arena_alloc(&a, 0));
	ASSERT_EQ(0, a.chunks[0].used);
}

static TEST(arena_try_alloc_exhausts_and_returns_null)
{
	char buf[32];
	struct gremlind_arena a;

	gremlind_arena_init(&a, buf, sizeof(buf), NULL, NULL);

	ASSERT_NOT_NULL(gremlind_arena_try_alloc(&a, 16));
	ASSERT_NOT_NULL(gremlind_arena_try_alloc(&a, 16));
	/* Full; no growth configured. */
	ASSERT_NULL(gremlind_arena_try_alloc(&a, 1));
	ASSERT_NULL(gremlind_arena_alloc(&a, 1));
}

static TEST(arena_auto_grows_via_callback)
{
	struct gremlind_arena a;

	ASSERT_TRUE(gremlind_arena_init_malloc(&a, 32));

	/* Fill first chunk, then allocate past its end — should auto-grow. */
	void *p1 = gremlind_arena_alloc(&a, 24);
	void *p2 = gremlind_arena_alloc(&a, 24);
	void *p3 = gremlind_arena_alloc(&a, 24);

	ASSERT_NOT_NULL(p1);
	ASSERT_NOT_NULL(p2);
	ASSERT_NOT_NULL(p3);
	ASSERT_TRUE(a.n_chunks >= 2);

	/* p1 must still be in the first chunk — chunks never move. */
	ASSERT_TRUE((char *)p1 >= a.chunks[0].base);
	ASSERT_TRUE((char *)p1 < a.chunks[0].base + a.chunks[0].cap);

	gremlind_arena_free_malloc(&a);
}

static TEST(arena_push_chunk_then_alloc)
{
	char buf1[16];
	char buf2[64];
	struct gremlind_arena a;

	gremlind_arena_init(&a, buf1, sizeof(buf1), NULL, NULL);

	ASSERT_NOT_NULL(gremlind_arena_alloc(&a, 16));
	ASSERT_NULL(gremlind_arena_alloc(&a, 8));

	ASSERT_TRUE(gremlind_arena_push_chunk(&a, buf2, sizeof(buf2)));
	ASSERT_EQ(2, a.n_chunks);

	void *p = gremlind_arena_alloc(&a, 8);
	ASSERT_NOT_NULL(p);
	ASSERT_TRUE((char *)p >= buf2);
	ASSERT_TRUE((char *)p < buf2 + sizeof(buf2));
}

static TEST(arena_push_chunk_rejects_when_full)
{
	char buf[16];
	struct gremlind_arena a;

	gremlind_arena_init(&a, buf, sizeof(buf), NULL, NULL);

	for (size_t i = 1; i < GREMLIND_MAX_CHUNKS; i++) {
		ASSERT_TRUE(gremlind_arena_push_chunk(&a, buf, sizeof(buf)));
	}
	ASSERT_EQ(GREMLIND_MAX_CHUNKS, a.n_chunks);

	ASSERT_FALSE(gremlind_arena_push_chunk(&a, buf, sizeof(buf)));
	ASSERT_EQ(GREMLIND_MAX_CHUNKS, a.n_chunks);
}

static TEST(arena_pointers_stay_stable_across_chunks)
{
	char buf1[16];
	char buf2[16];
	struct gremlind_arena a;

	gremlind_arena_init(&a, buf1, sizeof(buf1), NULL, NULL);

	void *p1 = gremlind_arena_alloc(&a, 8);
	ASSERT_NOT_NULL(p1);

	ASSERT_TRUE(gremlind_arena_push_chunk(&a, buf2, sizeof(buf2)));

	void *p2 = gremlind_arena_alloc(&a, 8);
	ASSERT_NOT_NULL(p2);

	ASSERT_TRUE((char *)p1 >= buf1 && (char *)p1 < buf1 + sizeof(buf1));
	ASSERT_TRUE((char *)p2 >= buf2 && (char *)p2 < buf2 + sizeof(buf2));
}

void arena_test(void);

void arena_test(void)
{
	RUN_TEST(arena_init_single_chunk);
	RUN_TEST(arena_alloc_basic);
	RUN_TEST(arena_alloc_rounds_to_alignment);
	RUN_TEST(arena_alloc_zero_returns_null);
	RUN_TEST(arena_try_alloc_exhausts_and_returns_null);
	RUN_TEST(arena_auto_grows_via_callback);
	RUN_TEST(arena_push_chunk_then_alloc);
	RUN_TEST(arena_push_chunk_rejects_when_full);
	RUN_TEST(arena_pointers_stay_stable_across_chunks);
}

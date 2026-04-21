#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gremlin.h"
#include "example.pb.h"

static void
print_bytes(const char *label, struct gremlin_bytes b)
{
	printf("  %s: \"%.*s\" (%zu bytes)\n",
	       label, (int)b.len, (const char *)b.data, b.len);
}

static const char *
role_name(int32_t r)
{
	switch (r) {
	case example_Role_GUEST: return "GUEST";
	case example_Role_USER:  return "USER";
	case example_Role_ADMIN: return "ADMIN";
	default:                 return "?";
	}
}

int
main(void)
{
	/* Writer structs are plain-old-data; sub-messages are passed by
	 * pointer and the writer takes no ownership. */
	example_Address addr = {
		.city    = { .data = (const uint8_t *)"Berlin",  .len = 6  },
		.country = { .data = (const uint8_t *)"Germany", .len = 7  },
		.zip     = 10115,
	};

	struct gremlin_bytes tags[] = {
		{ .data = (const uint8_t *)"admin",  .len = 5 },
		{ .data = (const uint8_t *)"member", .len = 6 },
	};

	example_Person p = {
		.name       = { .data = (const uint8_t *)"Ada Lovelace", .len = 12 },
		.age        = 36,
		.role       = example_Role_ADMIN,
		.address    = &addr,
		.tags       = tags,
		.tags_count = sizeof tags / sizeof tags[0],
	};

	/* Two-step encode: `_size` returns the exact byte count and caches
	 * it on the struct (used for nested-message length prefixes);
	 * `_encode` writes into a caller-provided writer. No heap use
	 * inside the runtime — we own the buffer. */
	size_t need = example_Person_size(&p);
	uint8_t *buf = malloc(need);
	if (buf == NULL) return 1;

	struct gremlin_writer w;
	gremlin_writer_init(&w, buf, need);
	example_Person_encode(&p, &w);
	printf("encoded %zu bytes\n", w.offset);

	/* Readers are zero-copy views over the caller's buffer: `_reader_init`
	 * walks the wire format once to cache field offsets; `_reader_get_*`
	 * then returns values (or sub-readers) without touching the heap. */
	example_Person_reader r;
	enum gremlin_error err = example_Person_reader_init(&r, buf, w.offset);
	if (err != GREMLIN_OK) {
		fprintf(stderr, "decode failed: error %d\n", err);
		free(buf);
		return 1;
	}

	printf("\nPerson:\n");
	print_bytes("name", example_Person_reader_get_name(&r));
	printf("  age:  %d\n", example_Person_reader_get_age(&r));
	printf("  role: %s\n", role_name(example_Person_reader_get_role(&r)));

	example_Address_reader addr_r;
	err = example_Person_reader_get_address(&r, &addr_r);
	if (err == GREMLIN_OK) {
		printf("  address:\n");
		print_bytes("    city",    example_Address_reader_get_city(&addr_r));
		print_bytes("    country", example_Address_reader_get_country(&addr_r));
		printf("    zip:  %d\n", example_Address_reader_get_zip(&addr_r));
	}

	printf("  tags (%zu):\n", example_Person_reader_tags_count(&r));
	example_Person_reader_tags_iter it = example_Person_reader_tags_begin(&r);
	for (;;) {
		struct gremlin_bytes tag;
		err = example_Person_reader_tags_next(&it, &tag);
		if (err != GREMLIN_OK || tag.len == 0) break;
		print_bytes("   ", tag);
	}

	free(buf);
	return 0;
}

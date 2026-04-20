#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "sources.h"

static bool
sources_push(struct source_list *list, char *path, char *buf, size_t len)
{
	if (list->count == list->cap) {
		size_t new_cap = list->cap == 0 ? 8 : list->cap * 2;
		struct source_slot *n = realloc(list->items,
						new_cap * sizeof(*n));
		if (n == NULL) return false;
		list->items = n;
		list->cap = new_cap;
	}
	list->items[list->count++] = (struct source_slot){
		.path = path, .buf = buf, .len = len,
	};
	return true;
}

bool
sources_load(struct source_list *list, const char *imports_root,
	     const char *rel_path)
{
	size_t len = 0;
	char *buf = read_under_root(imports_root, rel_path, &len);
	if (buf == NULL) {
		fprintf(stderr, "gremlinc-gen: cannot open %s/%s: %s\n",
			imports_root, rel_path, strerror(errno));
		return false;
	}

	char *path = strdup(rel_path);
	if (path == NULL) {
		free(buf);
		fprintf(stderr, "gremlinc-gen: out of memory\n");
		return false;
	}

	if (!sources_push(list, path, buf, len)) {
		free(path);
		free(buf);
		fprintf(stderr, "gremlinc-gen: out of memory\n");
		return false;
	}
	return true;
}

void
sources_free(struct source_list *list)
{
	for (size_t i = 0; i < list->count; i++) {
		free(list->items[i].path);
		free(list->items[i].buf);
	}
	free(list->items);
	list->items = NULL;
	list->count = 0;
	list->cap = 0;
}

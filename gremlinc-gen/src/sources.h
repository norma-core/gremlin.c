#ifndef _GREMLINC_GEN_SOURCES_H_
#define _GREMLINC_GEN_SOURCES_H_

#include <stdbool.h>
#include <stddef.h>

/*
 * One loaded .proto file. `path` is the logical key (what would appear
 * inside an `import "..."`) — stored verbatim from the CLI argument,
 * so `foo.proto` matches against `import "foo.proto"`. `buf` is the
 * malloc'd file content.
 */
struct source_slot {
	char	*path;
	char	*buf;
	size_t	 len;
};

struct source_list {
	struct source_slot	*items;
	size_t			 count;
	size_t			 cap;
};

/*
 * Load `<imports_root>/<rel_path>` and append it to `list` with
 * `rel_path` as its logical key. No transitive discovery — every file
 * the caller wants compiled (including imports) must be loaded
 * explicitly. An import that references a path not in the list will
 * fail at the `gremlind_link_imports` stage.
 *
 * Returns true on success. On any I/O error (file not found, OOM,
 * etc.) prints a diagnostic to stderr and returns false.
 */
bool	sources_load(struct source_list *list,
		     const char *imports_root,
		     const char *rel_path);

/*
 * Free every slot's path + buf and the backing items array.
 */
void	sources_free(struct source_list *list);

#endif /* !_GREMLINC_GEN_SOURCES_H_ */

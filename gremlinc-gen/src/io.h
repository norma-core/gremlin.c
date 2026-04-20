#ifndef _GREMLINC_GEN_IO_H_
#define _GREMLINC_GEN_IO_H_

#include <stddef.h>

/*
 * Read an entire file into a malloc'd, NUL-terminated buffer. Returns
 * NULL on any error (caller inspects errno). `*out_len` gets the
 * file's byte length (excluding the trailing NUL) when non-NULL.
 */
char	*slurp(const char *path, size_t *out_len);

/*
 * Read `<imports_root>/<rel>` into a malloc'd NUL-terminated buffer.
 * On success `*out_len` is the byte length (excluding the NUL).
 * Returns NULL on any error.
 */
char	*read_under_root(const char *imports_root, const char *rel, size_t *out_len);

/*
 * Build `<out_dir>/<src_path>.pb.h` for a given source path, preserving
 * any subdirectories. `a/b/c.proto` → `<out_dir>/a/b/c.pb.h`. The
 * `.proto` suffix is stripped if present. Result is malloc'd; caller
 * frees.
 */
char	*derive_output_path(const char *out_dir, const char *src_path);

/*
 * Create every intermediate directory on the path to `file_path` (the
 * file itself is not created). Existing directories are fine. Returns
 * 0 on success, -1 on I/O error (caller inspects errno).
 */
int	 ensure_parent_dir(const char *file_path);

#endif /* !_GREMLINC_GEN_IO_H_ */

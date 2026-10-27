#ifndef _GREMLINC_GEN_EMIT_FILE_H_
#define _GREMLINC_GEN_EMIT_FILE_H_

#include "gremlind/lib.h"
#include "gremlinc/lib.h"

/*
 * Write one `<stem>.pb.h` body into `w`:
 *
 *   #ifndef <GUARD>
 *   #define <GUARD>
 *   #include "gremlin.h"
 *   <enums in declaration order>
 *   <forward decls for back-edge message targets>
 *   <messages in topo order>
 *   #endif
 *
 * `global_scope` is the shared C-identifier scope (pre-populated by
 * `gremlinc_assign_c_names` for every descriptor across every file);
 * it's threaded through the emit-message helpers for per-member
 * mangling. `reserved` holds the public names of every file, which a
 * predicate leaves alone.
 * `logic_scope` holds the write-side logic names of every file.
 *
 * `src_path` is the logical path of this source file — used only to
 * derive the header guard (`FOO_PROTO_PB_H_` etc.).
 */
enum gremlinp_parsing_error	emit_file(struct gremlind_arena *arena,
					  struct gremlinc_name_scope *global_scope,
					  const struct gremlinc_name_scope *reserved,
					  struct gremlinc_name_scope *logic_scope,
					  const struct gremlind_file *file,
					  const char *src_path,
					  struct gremlinc_writer *w);

/* Whether a message of `file` is named by a contract before it is emitted,
 * as in mutual recursion; a message holding only itself is not. */
enum gremlinp_parsing_error	emit_file_names_ahead(struct gremlind_arena *arena,
						      const struct gremlind_file *file,
						      bool *out);

#endif /* !_GREMLINC_GEN_EMIT_FILE_H_ */

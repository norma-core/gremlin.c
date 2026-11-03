#ifndef _GREMLIND_BUILD_H_
#define _GREMLIND_BUILD_H_

#include <stdbool.h>
#include <stddef.h>

#include "arena.h"
#include "nodes.h"

#include "gremlinp/buffer.h"
#include "gremlinp/errors.h"

/*
 * Walk gremlinp's top-level iterator on a caller-provided parser buffer
 * and materialise a gremlind_file in the arena. The caller is
 * responsible for initialising `buf` (typically via
 * gremlinp_parser_buffer_init) and for keeping the backing char buffer
 * alive for the AST's lifetime — spans in the resulting nodes point
 * directly into it.
 *
 * On success, result.file is a valid arena-allocated node and
 * result.error == GREMLINP_OK. On parse error, result.file is NULL and
 * result.error carries the gremlinp error; result.error_offset is the
 * parser offset where the error was detected. On arena exhaustion,
 * result.error == GREMLINP_ERROR_OUT_OF_MEMORY.
 *
 * Two-pass: the builder counts repeated entries first and then
 * allocates exact-size arrays. The parser buffer offset is saved on
 * entry and restored before the second pass, then advanced to the end
 * of the parsed region on return.
 */
struct gremlind_build_result {
	struct gremlind_file		*file;
	enum gremlinp_parsing_error	error;
	size_t				error_offset;
};

struct gremlind_build_result	gremlind_build_file(struct gremlind_arena *arena,
						    struct gremlinp_parser_buffer *buf);

/* What a field's options say on packing: `[packed = ...]` or
 * `features.repeated_field_encoding`; -1 when nothing, else 0 or 1. */
int	gremlind_field_packed_option(const struct gremlinp_parser_buffer *buf,
				     const struct gremlinp_option_list_result *opts);

/* Packing of a repeated scalar in `file` that says nothing itself. */
bool	gremlind_file_packed_default(const struct gremlind_file *file);

#endif /* !_GREMLIND_BUILD_H_ */

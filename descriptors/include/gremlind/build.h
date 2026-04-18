#ifndef _GREMLIND_BUILD_H_
#define _GREMLIND_BUILD_H_

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

#endif /* !_GREMLIND_BUILD_H_ */

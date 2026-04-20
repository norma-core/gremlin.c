#ifndef _GREMLINC_WRITER_H_
#define _GREMLINC_WRITER_H_

#include <stdbool.h>
#include <stddef.h>

#include "gremlinp/errors.h"

/*
 * Text writer — same shape as parser's gremlinp_parser_buffer and
 * runtime's gremlin_writer, but for emitting generated source text
 * into a caller-provided char buffer. Bump cursor, overflow-safe.
 *
 * Caller provides the buffer (typically arena-backed, sized by a
 * caller-side upper-bound or grown on demand). Every emitter returns
 * GREMLINP_ERROR_BUFFER_OVERFLOW on insufficient space; the offset
 * still advances up to the last successful write.
 */
struct gremlinc_writer {
	char	*buf;
	size_t	 cap;
	size_t	 offset;
	bool	 owns_buf;	/* true → realloc-grown on overflow, free via dispose */
};

/* Caller-provided fixed buffer; overflow is a hard error. Used by tests
 * and any emission site that wants to cap output size. */
void					gremlinc_writer_init(struct gremlinc_writer *w, char *buf, size_t cap);

/* Writer owns its buffer: malloc `initial_cap` bytes, grow by doubling
 * via realloc on overflow. Returns false on malloc failure. Pair with
 * gremlinc_writer_dispose to free. */
bool					gremlinc_writer_init_owned(struct gremlinc_writer *w, size_t initial_cap);

/* Free the writer's owned buffer. No-op on non-owned writers. */
void					gremlinc_writer_dispose(struct gremlinc_writer *w);

/* Append `len` bytes from `s` to the writer. OVERFLOW if cap exceeded. */
enum gremlinp_parsing_error		gremlinc_write(struct gremlinc_writer *w, const char *s, size_t len);

/* Append a null-terminated C string. */
enum gremlinp_parsing_error		gremlinc_write_cstr(struct gremlinc_writer *w, const char *s);

/* Append the decimal form of a signed 32-bit integer (for enum values, field indices). */
enum gremlinp_parsing_error		gremlinc_write_i32(struct gremlinc_writer *w, int32_t v);

/*
 * Collapse a run of trailing `\n`s down to a single newline — style(9)
 * wants exactly one newline at end-of-file. Emit functions add a blank
 * line after each typedef for separation; the FINAL typedef leaves a
 * trailing double-newline that this call trims.
 */
void					gremlinc_writer_trim_trailing_blank(struct gremlinc_writer *w);

#endif /* !_GREMLINC_WRITER_H_ */

#ifndef _GREMLIND_NAME_H_
#define _GREMLIND_NAME_H_

#include <stdbool.h>
#include <stddef.h>

#include "arena.h"

#include "gremlinp/errors.h"

/*
 * Scoped (dotted) names for descriptors and type references.
 *
 * A scoped name is an ordered list of segments. Each segment is a span
 * (const char *, size_t length) pointing directly into a .proto source
 * buffer — no string copies. Only the segment-array itself is
 * arena-allocated.
 *
 * Example: the FQN of message C in
 *
 *     package foo.bar;
 *     message A { message B { message C {} } }
 *
 * is a 5-segment name with segments pointing at "foo", "bar", "A",
 * "B", "C" inside the source buffer.
 *
 * Comparison is per-segment memcmp — no concatenation, no allocation.
 *
 * The `absolute` flag matters only for type *references*: protobuf
 * syntax uses a leading "." (e.g. ".foo.Bar") to mark a reference as
 * already-absolute, skipping the scope walk. Descriptors built by
 * gremlind_compute_scoped_names have their FQN absolute-by-construction
 * and set absolute = true.
 */

struct gremlind_name_segment {
	const char	*start;		/* into source buffer */
	size_t		 length;
};

struct gremlind_scoped_name {
	struct gremlind_name_segment	*segments;	/* arena-allocated */
	size_t				 n_segments;
	bool				 absolute;
};

/*
 * Two scoped names are equal iff they have the same segment count and
 * every segment pair is byte-equal. The `absolute` flag is NOT compared
 * — callers decide whether to require absolute-matching before comparing.
 */
bool gremlind_scoped_name_eq(const struct gremlind_scoped_name *a,
			      const struct gremlind_scoped_name *b);

/*
 * Parse a dotted-name span (e.g. "foo.bar.Baz" or ".foo.bar.Baz") into
 * a scoped name. A leading '.' sets absolute = true and is not itself a
 * segment. Empty segments (leading non-`.` dot, trailing dot, or "..")
 * are rejected with GREMLINP_ERROR_INVALID_OPTION_NAME.
 */
enum gremlinp_parsing_error
gremlind_scoped_name_parse(struct gremlind_arena *arena,
			    const char *span, size_t span_len,
			    struct gremlind_scoped_name *out);

/*
 * Construct a new scoped name by concatenating `prefix` and `extra`
 * segments into a fresh arena-allocated array. Neither input is
 * mutated. `out->absolute` is set to true (descriptors are always
 * absolute).
 */
enum gremlinp_parsing_error
gremlind_scoped_name_extend(struct gremlind_arena *arena,
			     const struct gremlind_scoped_name *prefix,
			     const struct gremlind_name_segment *extra,
			     size_t n_extra,
			     struct gremlind_scoped_name *out);

/*
 * Post-build pass: fills `scoped_name` on every message and enum in the
 * file. Requires `gremlind_build_file` to have already run on `file`.
 * For each descriptor D, D->scoped_name.segments =
 *   (package segments) ++ (D's parent chain names, root→leaf) ++ [D's own name]
 *
 * Messages are processed first (DFS pre-order means parents are ready
 * when children are reached), then enums (their parent messages are
 * already named).
 */
struct gremlind_file;	/* from nodes.h */
enum gremlinp_parsing_error
gremlind_compute_scoped_names(struct gremlind_arena *arena,
			       struct gremlind_file *file);

#endif /* !_GREMLIND_NAME_H_ */

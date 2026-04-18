#include <string.h>

#include "gremlind/arena.h"
#include "gremlind/name.h"
#include "gremlind/nodes.h"

bool
gremlind_scoped_name_eq(const struct gremlind_scoped_name *a,
			 const struct gremlind_scoped_name *b)
{
	if (a->n_segments != b->n_segments) return false;

	for (size_t i = 0; i < a->n_segments; i++) {
		if (a->segments[i].length != b->segments[i].length) return false;
		if (a->segments[i].length > 0 &&
		    memcmp(a->segments[i].start, b->segments[i].start,
			   a->segments[i].length) != 0) {
			return false;
		}
	}
	return true;
}

enum gremlinp_parsing_error
gremlind_scoped_name_parse(struct gremlind_arena *arena,
			    const char *span, size_t span_len,
			    struct gremlind_scoped_name *out)
{
	out->segments = NULL;
	out->n_segments = 0;
	out->absolute = false;

	size_t i = 0;

	if (span_len > 0 && span[0] == '.') {
		out->absolute = true;
		i = 1;
	}

	if (i >= span_len) {
		/* "" or "." alone — no segments. */
		return GREMLINP_ERROR_INVALID_OPTION_NAME;
	}

	/* Count dots in the remaining portion to size the segment array. */
	size_t n_segs = 1;
	for (size_t k = i; k < span_len; k++) {
		if (span[k] == '.') n_segs++;
	}

	struct gremlind_name_segment *segs = gremlind_arena_alloc(arena,
		n_segs * sizeof(*segs));
	if (segs == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;

	size_t seg_start = i;
	size_t seg_idx = 0;
	for (size_t k = i; k < span_len; k++) {
		if (span[k] == '.') {
			if (k == seg_start) {
				/* Empty segment — ".." or leading/trailing dot
				 * inside the name. */
				return GREMLINP_ERROR_INVALID_OPTION_NAME;
			}
			segs[seg_idx].start = span + seg_start;
			segs[seg_idx].length = k - seg_start;
			seg_idx++;
			seg_start = k + 1;
		}
	}
	if (seg_start >= span_len) {
		/* Trailing dot. */
		return GREMLINP_ERROR_INVALID_OPTION_NAME;
	}
	segs[seg_idx].start = span + seg_start;
	segs[seg_idx].length = span_len - seg_start;

	out->segments = segs;
	out->n_segments = n_segs;
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlind_scoped_name_extend(struct gremlind_arena *arena,
			     const struct gremlind_scoped_name *prefix,
			     const struct gremlind_name_segment *extra,
			     size_t n_extra,
			     struct gremlind_scoped_name *out)
{
	size_t total = prefix->n_segments + n_extra;
	out->segments = NULL;
	out->n_segments = 0;
	out->absolute = true;

	if (total == 0) return GREMLINP_OK;

	struct gremlind_name_segment *segs = gremlind_arena_alloc(arena,
		total * sizeof(*segs));
	if (segs == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;

	if (prefix->n_segments > 0) {
		memcpy(segs, prefix->segments,
		       prefix->n_segments * sizeof(*segs));
	}
	if (n_extra > 0) {
		memcpy(segs + prefix->n_segments, extra,
		       n_extra * sizeof(*segs));
	}

	out->segments = segs;
	out->n_segments = total;
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlind_compute_scoped_names(struct gremlind_arena *arena,
			       struct gremlind_file *file)
{
	/* Package segments — one parse, reused as the prefix for every
	 * top-level descriptor. Absent package means an empty prefix. */
	struct gremlind_scoped_name pkg;
	pkg.segments = NULL;
	pkg.n_segments = 0;
	pkg.absolute = true;

	if (file->package.present &&
	    file->package.value.name_length > 0) {
		enum gremlinp_parsing_error err = gremlind_scoped_name_parse(arena,
			file->package.value.name_start,
			file->package.value.name_length,
			&pkg);
		if (err != GREMLINP_OK) return err;
		pkg.absolute = true;
	}

	/* Messages first — DFS pre-order means parents come before children
	 * in file->messages, so every parent's scoped_name is already set
	 * when its child is processed. */
	for (size_t i = 0; i < file->messages.count; i++) {
		struct gremlind_message *m = &file->messages.items[i];

		struct gremlind_name_segment self = {
			.start = m->parsed.name_start,
			.length = m->parsed.name_length
		};

		const struct gremlind_scoped_name *prefix =
			(m->parent != NULL) ? &m->parent->scoped_name : &pkg;

		enum gremlinp_parsing_error err = gremlind_scoped_name_extend(
			arena, prefix, &self, 1, &m->scoped_name);
		if (err != GREMLINP_OK) return err;
	}

	/* Enums next — their parent (if any) is a message, whose
	 * scoped_name was set in the loop above. */
	for (size_t i = 0; i < file->enums.count; i++) {
		struct gremlind_enum *en = &file->enums.items[i];

		struct gremlind_name_segment self = {
			.start = en->parsed.name_start,
			.length = en->parsed.name_length
		};

		const struct gremlind_scoped_name *prefix =
			(en->parent != NULL) ? &en->parent->scoped_name : &pkg;

		enum gremlinp_parsing_error err = gremlind_scoped_name_extend(
			arena, prefix, &self, 1, &en->scoped_name);
		if (err != GREMLINP_OK) return err;
	}

	return GREMLINP_OK;
}

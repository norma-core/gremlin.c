#ifndef _GREMLIND_EXTEND_H_
#define _GREMLIND_EXTEND_H_

#include "resolve.h"

#include "gremlinp/errors.h"

/*
 * Extension propagation pass.
 *
 * Walks every file's parsed entries (top-level + nested-in-message),
 * picks out `extend T { field1; field2; ... }` declarations, resolves
 * the target message T against the extending file's visibility, and
 * appends the inner fields to the target message's fields[] array.
 *
 * The pass reparses the extending file's buffer to recover the field
 * declarations verbatim (the builder deliberately dropped extend
 * entries since they need target resolution, which requires
 * scoped_names + visibility to be ready). Appended fields carry an
 * `origin_file` back-pointer so type-ref resolution walks the
 * extending file's visibility set, not the target file's.
 *
 * Pipeline placement: after gremlind_compute_visibility (needs visible
 * set for target resolution), before gremlind_resolve_type_refs (so
 * the appended fields participate in the standard type-ref pass).
 *
 * Returns GREMLINP_OK on success. On target-not-found, returns
 * GREMLINP_ERROR_EXTEND_SOURCE_NOT_FOUND with ctx->failed_source_idx
 * set to the extending file.
 */
enum gremlinp_parsing_error
gremlind_propagate_extends(struct gremlind_resolve_context *ctx);

#endif /* !_GREMLIND_EXTEND_H_ */

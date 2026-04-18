#ifndef _GREMLIND_RESOLVE_H_
#define _GREMLIND_RESOLVE_H_

#include <stddef.h>

#include "arena.h"
#include "nodes.h"

#include "gremlinp/buffer.h"
#include "gremlinp/errors.h"

/*
 * Multi-file AST construction + import linking.
 *
 * A gremlind_source is one input file: a logical path (as it would
 * appear in an `import "..."` statement, caller-normalised) and a
 * parser buffer the caller has already initialised over its source.
 *
 * The resolve context bundles an arena, an input array of sources, and
 * the parallel output array of built files. It also collects error
 * state so functions can bail out and leave the caller with enough
 * context to report the failure.
 *
 * The caller owns: sources array (including each .path buffer and each
 * .buf's backing char array), arena, and the context struct itself.
 * gremlind allocates only into the arena: the output files[] array and
 * all the AST nodes it contains.
 */

struct gremlind_source {
	const char			*path;		/* caller-normalised key */
	size_t				path_len;
	struct gremlinp_parser_buffer	buf;		/* caller-initialised */
};

struct gremlind_resolve_context {
	/* inputs (caller-owned) */
	struct gremlind_arena		*arena;
	struct gremlind_source		*sources;
	size_t				n_sources;

	/* outputs (arena-allocated by build_all) */
	struct gremlind_file		**files;	/* parallel to sources */

	/* error state (valid when a function returns non-OK) */
	enum gremlinp_parsing_error	error;
	size_t				failed_source_idx;
	size_t				error_offset;
	struct gremlind_import		*failed_import;
	struct gremlind_field		*failed_field;	/* set on type-ref not-found */
};

/*
 * Zero the context and wire its inputs. Does not touch the arena.
 */
void				gremlind_resolve_context_init(struct gremlind_resolve_context *ctx,
							      struct gremlind_arena *arena,
							      struct gremlind_source *sources,
							      size_t n_sources);

/*
 * Parse + build every source into ctx->files. Bails on the first parse
 * error with ctx->failed_source_idx + ctx->error_offset populated.
 * Requires ctx->arena, ctx->sources, ctx->n_sources to be set; allocates
 * ctx->files in the arena.
 */
enum gremlinp_parsing_error	gremlind_build_all(struct gremlind_resolve_context *ctx);

/*
 * For every file, for every import, set import.resolved to the
 * gremlind_file* whose source.path matches the import's path span
 * (byte-equal). Requires ctx->files to already be populated (by a
 * prior gremlind_build_all). Bails on the first unresolved import
 * with ctx->failed_source_idx + ctx->failed_import populated.
 */
enum gremlinp_parsing_error	gremlind_link_imports(struct gremlind_resolve_context *ctx);

/*
 * Detect cycles in the resolved import graph. Requires gremlind_link_imports
 * to have run successfully first. Walks the graph with iterative DFS using a
 * three-colour marking (white / gray / black); if DFS encounters a gray
 * (currently-on-stack) node, that's a cycle.
 *
 * On cycle: returns GREMLINP_ERROR_CIRCULAR_IMPORT and sets
 * ctx->failed_source_idx to one file on the cycle. Scratch state (colours
 * and DFS stack) is arena-allocated; the caller should expect some arena
 * usage proportional to ctx->n_sources.
 */
enum gremlinp_parsing_error	gremlind_check_no_cycles(struct gremlind_resolve_context *ctx);

/*
 * Populate file->visible on every file in ctx. For file F:
 *
 *   F.visible = {F} ∪ (direct imports of F) ∪ (transitive public imports
 *                                              reachable from direct imports)
 *
 * Only `import public "..."` edges propagate beyond one hop — a plain
 * import gets you the directly-imported file's types and nothing more.
 * This matches protoc's DescriptorPool visibility semantics.
 *
 * Requires gremlind_link_imports to have run first. Running
 * gremlind_check_no_cycles beforehand is strongly recommended: this
 * function scales by public-chain length, and without cycle detection
 * an adversarial input could blow up.
 */
enum gremlinp_parsing_error	gremlind_compute_visibility(struct gremlind_resolve_context *ctx);

/*
 * Resolve every field's type reference. Uses protobuf's scope-walk rule:
 * for a field inside message M (FQN = M.scoped_name), a relative type
 * reference `X.Y.Z` is looked up as
 *
 *   M.scoped_name[0..k] ++ [X, Y, Z]   for k = |M.scoped_name| down to 0
 *
 * The first hit in any descriptor visible to F (i.e. inside
 * F.visible[*].messages or .enums) wins. Absolute references (leading '.')
 * skip the walk and do a single direct lookup. Builtin scalar types
 * (int32, string, bool, etc.) are recognised without any lookup.
 *
 * Requires gremlind_compute_scoped_names and gremlind_compute_visibility
 * to have run first. On first unresolved ref, returns
 * GREMLINP_ERROR_TYPE_NOT_FOUND with ctx->failed_source_idx +
 * ctx->failed_field populated.
 */
enum gremlinp_parsing_error	gremlind_resolve_type_refs(struct gremlind_resolve_context *ctx);

/*
 * Topological ordering of the files in ctx by their import graph. Files
 * with no deps come first, files that import everything come last. C
 * codegen walks it and emits X.h before any Y.h that #includes it.
 *
 * Requires gremlind_link_imports to have run. Callers should also run
 * gremlind_check_no_cycles first — on cyclic input the ordering of
 * cycle participants is undefined.
 */
struct gremlind_file_order {
	struct gremlind_file	**items;	/* arena-allocated, parallel to ctx->files */
	size_t			 count;
};

enum gremlinp_parsing_error	gremlind_topo_sort_files(struct gremlind_resolve_context *ctx,
							 struct gremlind_file_order *out);

/*
 * Topological ordering of messages WITHIN a single file by their
 * field-dependency graph. Leaves come first; messages whose fields use
 * other in-file messages come after. Enums are not included — they are
 * always leaves and can be emitted in declaration order.
 *
 * In-file cycles (self-reference `message M { M next = 1; }`, mutual
 * `A→B→A`, etc.) are preserved — every message appears exactly once in
 * `items` — and `predeclare` lists the exact subset of messages that
 * must be forward-declared before the topo-ordered struct definitions.
 * `predeclare` is the set of back-edge targets found during DFS, which
 * is exactly the set of types referenced before their definition when
 * `items` is emitted in order. C codegen emits
 * `typedef struct X X;` for each one before walking `items`.
 *
 * Requires gremlind_resolve_type_refs to have run first.
 */
struct gremlind_message_order {
	struct gremlind_message	**items;	/* all messages in topo order */
	size_t			 count;
	struct gremlind_message	**predeclare;	/* subset: need forward decl */
	size_t			 predeclare_count;
};

enum gremlinp_parsing_error	gremlind_topo_sort_messages(struct gremlind_arena *arena,
							    struct gremlind_file *file,
							    struct gremlind_message_order *out);

#endif /* !_GREMLIND_RESOLVE_H_ */

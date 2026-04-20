#ifndef _GREMLINC_EMIT_H_
#define _GREMLINC_EMIT_H_

#include "gremlind/nodes.h"
#include "gremlinp/errors.h"
#include "writer.h"
#include "naming.h"

/*
 * Emit an enum as a C typedef to `w`. Expected layout:
 *
 *     typedef enum <FQN> {
 *             <FQN>_<VALUE_1> = <n1>,
 *             <FQN>_<VALUE_2> = <n2>,
 *             ...
 *     } <FQN>;
 *
 * where <FQN> is the scoped name joined with `_`, passed through the
 * name-scope mangler (keyword + collision suffixing). Enum value
 * identifiers are also registered in the same scope so a later type
 * whose joined name matches an existing enum value would get a
 * `_N` suffix — at the cost of one extra ordinal per conflicting pair.
 *
 * Values are emitted in declaration order; C enum values match proto
 * field numbers (signed int32).
 *
 * Requires gremlind_compute_scoped_names to have run on the enum's
 * file beforehand so `e->scoped_name` is populated.
 */
enum gremlinp_parsing_error	gremlinc_emit_enum(struct gremlinc_writer *w,
						   struct gremlinc_name_scope *scope,
						   const struct gremlind_enum *e);

/*
 * Emit a message as a writer struct + encoder + reader struct + decoder,
 * following the reader/writer split from gremlin.zig:
 *
 *     typedef struct <FQN> { <fields> } <FQN>;
 *     static inline size_t <FQN>_size(const <FQN> *);
 *     static inline void   <FQN>_encode(const <FQN> *, struct gremlin_writer *);
 *     typedef struct <FQN>_reader {
 *             const uint8_t *src; size_t src_len;
 *             <cached fields>
 *     } <FQN>_reader;
 *     static inline enum gremlin_error <FQN>_reader_init(<FQN>_reader *, const uint8_t *, size_t);
 *     static inline <T> <FQN>_reader_get_<field>(const <FQN>_reader *);
 *
 * Covered field kinds: scalar numerics (int32/64, uint32/64, sint32/64,
 * fixed32/64, sfixed32/64, double, float, bool), strings and bytes, enums,
 * singular / repeated message references, packed and unpacked repeated
 * scalars, maps, and oneof fields (lowered to optional fields with an
 * at-most-one-set invariant).
 *
 * Proto3 implicit presence: encode suppresses zero-valued fields; decode
 * returns zero for missing fields. Has-bits are materialised in the
 * reader struct so proto2 optional defaults and absent-on-wire vs
 * set-to-zero ambiguity can be resolved correctly.
 *
 * Requires gremlind_compute_scoped_names and gremlind_resolve_type_refs
 * to have run on the message's file beforehand.
 */
enum gremlinp_parsing_error	gremlinc_emit_message(struct gremlinc_writer *w,
						      struct gremlinc_name_scope *scope,
						      const struct gremlind_message *m);

/*
 * Emit the forward declarations for an emittable message:
 *
 *     typedef struct <M> <M>;
 *     typedef struct <M>_reader <M>_reader;
 *     static inline size_t <M>_size(const <M> *m);
 *     static inline void   <M>_encode(const <M> *m, struct gremlin_writer *w);
 *     static inline enum gremlin_error
 *         <M>_reader_init(<M>_reader *r, const uint8_t *src, size_t len);
 *
 * Must be emitted BEFORE any message body that references `<M>` — including
 * `<M>` itself, for self-recursive fields.  The tool emits forward decls
 * for every emittable message in a file before emitting any bodies.
 *
 * Requires `m->c_name` (set by `gremlinc_assign_c_names`).
 */
enum gremlinp_parsing_error	gremlinc_emit_message_forward(struct gremlinc_writer *w,
							      const struct gremlind_message *m);

#endif /* !_GREMLINC_EMIT_H_ */

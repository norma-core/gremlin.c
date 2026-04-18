# gremlind — protobuf descriptor layer

> Active development.

Built on top of [`gremlinp`](../parser/README.md), the formally verified
protobuf syntax parser. Where `gremlinp` returns spans and positional
entries via iterators, `gremlind` materialises a protoc-style
`DescriptorPool`: flat, name-indexed, with imports linked, cycles
rejected, scoped names computed, type references resolved, and
topological orders available for C codegen.

## Design in one paragraph

The parser is zero-allocation; this layer is not. Descriptors need
storage, sizes aren't predictable from source length. `gremlind` uses
a chunked bump-pointer arena with geometric growth — the caller seeds
an initial chunk, and `gremlind_arena_alloc` requests larger chunks
via a malloc-backed callback when the current one fills. Chunks
never move, so every descriptor pointer stays stable. All span fields
still reference the caller's source buffers — no string copies, only
the few arrays (flat `messages` / `enums` lists, scoped-name segment
arrays, visibility sets) live in the arena.

## Scope for v1

- **Build pass**: file → flat descriptors (every message / enum, all
  nesting levels, with `parent` back-pointers).
- **Imports**: `link_imports` fills resolved pointers by byte-equal
  path match (caller provides the logical path). `check_no_cycles`
  matches `protoc`'s reject-all-cycles semantics.
- **Names**: `compute_scoped_names` builds each descriptor's FQN as a
  segment array into the source.
- **Visibility**: `compute_visibility` — `{self} ∪ direct imports ∪
  transitive public imports`, matching `protoc`'s DescriptorPool.
- **Type refs**: `resolve_type_refs` does protobuf's scope walk for
  every field.
- **Codegen order**: `topo_sort_files` and `topo_sort_messages`. The
  latter also returns a `predeclare` set — the exact messages that
  need forward declarations for C's sake.

Out of scope for v1: options resolution, extensions, field-number /
reserved validation, oneofs, services.

## Formal verification

Only the verified **arena primitives** (`init`, `push_chunk`,
`try_alloc`, `bytes_used`) are discharged by Frama-C WP:
**115 / 115 goals + 16 / 16 smoke tests pass**, run by `make verify-arena`.

The auto-grow wrapper (`gremlind_arena_alloc`) has an admitted ACSL
contract — the body calls an opaque grow callback WP can't reason about.
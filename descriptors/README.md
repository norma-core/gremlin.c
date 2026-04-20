# 🧬 gremlind — protobuf descriptor pool

![version](https://img.shields.io/badge/version-0.1.0--dev-orange)
![status](https://img.shields.io/badge/status-active%20development-yellow)
![language](https://img.shields.io/badge/C-C99-blue)
![verified](https://img.shields.io/badge/Frama--C%20WP-arena-brightgreen)

> 🚧 **Active development.** The descriptor API, pipeline stages, and
> verification boundary are all still moving. Pin a commit if you
> depend on it.

`gremlind` is the 🧱 descriptor layer on top of
[`gremlinp`](../parser/README.md) — a formally-verified protobuf 3 /
editions syntax parser. Where `gremlinp` streams spans and positional
entries, `gremlind` materialises a `protoc`-style `DescriptorPool`:
flat, name-indexed, with imports linked, cycles rejected, scoped names
computed, type references resolved, `extend` fields propagated, and
topological orders available for C codegen.

The library is a staged pipeline of 📐 pure passes — each one takes a
`gremlind_resolve_context` and either populates fields on the
descriptors or sets `ctx->error` with enough position data to report
the failure.

## 📑 Table of contents

- [Scope and non-goals](#-scope-and-non-goals)
- [The pipeline](#-the-pipeline)
- [Using the library](#-using-the-library)
- [Building](#-building)
- [Formal verification](#-formal-verification)
  - [What is proven](#-what-is-proven)
  - [What is not verified](#-what-is-not-verified)
  - [Running verification](#️-running-verification)
- [Layout](#-layout)

## 🎯 Scope and non-goals

**✅ What this library is:**
- A descriptor builder — turns parsed `gremlinp` output into a flat,
  cross-referenced tree of `gremlind_message`, `gremlind_enum`,
  `gremlind_field`, `gremlind_import`, etc.
- Arena-allocated — all descriptor storage lives in a chunked
  bump-pointer arena the caller provides; no hidden `malloc`.
- Span-preserving — every name / type / path inside a descriptor is a
  `{const char *start, size_t length}` pointer into the caller's
  source buffer. No string copies.

**❌ What this library is not:**
- A semantic validator. Some well-formedness checks (cycle detection,
  type-ref resolution, extend target lookup) are pipeline stages, but
  full `protoc`-compatible semantic checking (option resolution,
  field-number / reserved validation, service method coverage) is out
  of scope for v1.
- A code generator. That's [`gremlinc`](../gremlinc/README.md), which
  consumes `gremlind` output.
- A parser. The actual tokenisation + grammar parsing is
  [`gremlinp`](../parser/README.md); `gremlind` only calls its
  iterators.

## 🪜 The pipeline

A typical end-to-end use of `gremlind` runs these stages in order on
a `gremlind_resolve_context`:

| Stage | Reads | Writes | Purpose |
| --- | --- | --- | --- |
| 🏗️  `gremlind_build_all` | `ctx->sources[].buf` | `ctx->files[]` | Parse each `.proto` source with `gremlinp`; materialise flat `messages[]` / `enums[]` / `imports[]` / `fields[]` arrays per file. |
| 🔗 `gremlind_link_imports` | `ctx->files[].imports[].parsed.path_*` | `imports[].resolved` | Point each import's `resolved` pointer at the matching file (by logical path). |
| 🔄 `gremlind_check_no_cycles` | `imports[].resolved` | `ctx->error` on failure | DFS over the import graph; reject any strongly-connected cycle. |
| 🏷️ `gremlind_compute_scoped_names` | `package`, `messages[]`, `enums[]` | `<descriptor>.scoped_name` | Walk nested-descriptor DFS; produce each descriptor's fully-qualified `[pkg..., parent..., self]` segment array. |
| 👀 `gremlind_compute_visibility` | `imports[].resolved`, `.parsed.type` | `files[].visible` | `{self} ∪ direct imports ∪ transitive public imports`. Matches protoc's visibility semantics. |
| 🧩 `gremlind_propagate_extends` | `extend T { ... }` entries | `<target>.fields[]` | Resolve each `extend` block's target by scoped name, append the declared fields to the target message. Preserves the extending file's visibility for the appended fields via `field.origin_file`. |
| 🎯 `gremlind_resolve_type_refs` | `fields[].parsed.type`, `visible[]` | `fields[].type` (= `builtin` / `message` / `enum` / `map`) | Builtin scalar names are recognised directly; named types use protobuf's scope-walk rule (deepest prefix first) against the owning file's `visible[]`. |
| 📦 `gremlind_topo_sort_files` | `imports[].resolved` | `gremlind_file_order` | Post-order topo sort so codegen can emit `A.pb.h` before anything that `#include`s it. |
| 🌲 `gremlind_topo_sort_messages` | `fields[].type.u.message` | `gremlind_message_order` (+ `predeclare[]`) | Same, per-file, over the field-reference graph. The `predeclare` subset is the exact messages that need forward `typedef struct X X;` declarations for C mutual-recursion support. |

Each pass is idempotent after its prerequisites have run — the context
carries state, not the caller.

## 📚 Using the library

The public entry point is `#include "gremlind/lib.h"`. The usual
workflow is:

1. 📝 Slurp each `.proto` file into a `char *` the caller owns.
2. 🧱 Initialise the arena (`gremlind_arena_init_malloc`).
3. 🎞️ Fill a `struct gremlind_source[]` with `{path, path_len, buf}`.
4. 🔄 Run the pipeline stages in the order described above.
5. 🔍 Walk the resolved AST (`ctx->files[i]->messages`, etc.).

### 🧪 A full worked example

`tests/integration_test.c` drives the entire pipeline over
`tests/proto_corpus/` (which contains the Google proto3 / unittest
corpus — 10+ files, mutual imports, public imports, deeply-nested
messages, extends) and asserts resolution results end-to-end. It is
the canonical example of how to use the library from start to finish.

## 🔨 Building

Dependencies: a C99 compiler and CMake ≥ 3.10. The parser is linked in
as sources (not as a separate static lib) so the whole stack can be
verified or instrumented in one pass.

```bash
mkdir build && cd build
cmake ..
make
```

Artifacts:
- 📦 `libgremlind.a` — the static library (also statically links the
  parser sources).
- 🧪 `descriptors_tests` — the test runner. Run with `./descriptors_tests`.

## 🔬 Formal verification

`gremlind` is **not fully verified end-to-end** — the descriptor
pipeline stages are imperative passes over arena-backed heap data, and
the WP-vs-SMT reasoning cost for that far exceeds the cost of the
parser's span-in / span-out contracts. Instead, the verified subset
is the 🧱 **arena primitives** — the one component everything else in
the pipeline relies on for memory safety.

### ✅ What is proven

**115 / 115 goals + 16 / 16 smoke tests pass**, run by `make verify-arena`:

| Group            | Proved      | Smoke    | Notes                              |
| ---------------- | ----------- | -------- | ---------------------------------- |
| 🧱 `verify-arena` | 115 / 115   | 16 / 16  | `init`, `push_chunk`, `try_alloc`, `bytes_used` |

For the verified primitives, WP proves:

- 🛡️ **Memory safety** — no out-of-bounds reads, no null derefs, no
  integer overflow (via `-wp-rte` runtime-error guards).
- 📐 **Pointer stability** — chunks never move once pushed; every
  allocation pointer stays valid until the arena is freed.
- 📏 **Alignment** — every allocation is `GREMLIND_ALIGN`-aligned
  (8 bytes).
- ➡️ **Monotonic accounting** — `bytes_used` never decreases between
  successful `try_alloc` calls.

The auto-grow wrapper `gremlind_arena_alloc` has an **admitted** ACSL
contract — its body calls an opaque function pointer (the grow
callback) that WP can't reason about without per-callee contracts.
The contract is faithful to the underlying primitives and documented
inline in `arena.h`.

### ❓ What is not verified

- 🪜 **The pipeline stages** — `build`, `link_imports`,
  `compute_scoped_names`, `compute_visibility`, `propagate_extends`,
  `resolve_type_refs`, `topo_sort_*`. These are imperative passes over
  arena memory, tested by `descriptors_tests` against the Google proto
  corpus but not covered by WP.
- 🧪 **The tests** in `tests/` — they are regular C integration tests,
  not proofs.
- 🔗 **Faithfulness to `protoc`** — visibility and scope-walk rules are
  tested against the Google corpus, not formally equivalent-checked
  against `protoc`'s own reference implementation.

### 🛠️ Running verification

You need Frama-C with the WP plugin plus Alt-Ergo and Z3. See the
[parser README](../parser/README.md#️-running-verification) for full
install instructions — the setup is identical.

From the build directory:

```bash
make verify          # all gremlind WP targets
make verify-arena    # arena primitives only
```

Proof results are cached in `<repo>/descriptors/.wp-cache/`; a cold
cache run takes about 30 seconds.

## 📁 Layout

```
descriptors/
├── include/gremlind/    — public headers
│   ├── lib.h            — umbrella header, #include this
│   ├── arena.h          — chunked bump-pointer arena (verified)
│   ├── axioms.h         — admitted ACSL predicates for arena
│   ├── std.h            — malloc-backed arena growth helpers
│   ├── nodes.h          — descriptor struct definitions
│   ├── build.h          — pipeline stage 1: parse → flat nodes
│   ├── resolve.h        — pipeline stages 2-5 (+ topo sorts)
│   ├── name.h           — scoped-name segment arrays + equality
│   └── extend.h         — pipeline stage 6: extend propagation
├── src/
│   ├── arena.c          — verified arena primitives
│   ├── arena_std.c      — malloc-backed grow callback (not verified)
│   ├── build.c          — 2-pass descriptor materialisation
│   ├── name.c           — scoped-name computation + parse + equality
│   ├── resolve.c        — imports / cycles / visibility / type refs / topo
│   ├── extend.c         — extend-field propagation pass
│   └── lib.c            — empty wrapper (kept for library linkage)
├── tests/               — integration + corpus tests (not verified)
│   └── proto_corpus/    — Google proto3 / unittest corpus fixtures
├── VERSION              — single source of truth for the version
└── CMakeLists.txt       — build + verify targets
```

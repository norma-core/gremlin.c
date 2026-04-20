# 🛠️ gremlinc — protobuf C codegen

![version](https://img.shields.io/badge/version-0.1.0--dev-orange)
![status](https://img.shields.io/badge/status-active%20development-yellow)
![language](https://img.shields.io/badge/C-C99-blue)
![verified](https://img.shields.io/badge/Frama--C%20WP-naming%20%2B%20const--convert-brightgreen)

> 🚧 **Active development.** The codegen output shape and per-family
> emit split are still moving. Pin a commit if you depend on it.

`gremlinc` is the 🏭 C code generator on top of
[`gremlind`](../descriptors/README.md) — a resolved protobuf descriptor
pool. Given a set of `.proto` files, it emits a `<file>.pb.h` per
input containing C `typedef`s for every enum and message, plus
`<M>_size` / `<M>_encode` / `<M>_reader_init` / `<M>_reader_get_<field>`
functions that `#include` [`gremlin.h`](../runtime/README.md) for all
wire-format primitives.

The generated code is `static inline` and header-only; the runtime
folds every primitive into the call site at `-O2`.

## 📑 Table of contents

- [Scope and non-goals](#-scope-and-non-goals)
- [The pipeline](#-the-pipeline)
- [Per-family emit modules](#-per-family-emit-modules)
- [Building](#-building)
- [Formal verification](#-formal-verification)
  - [What is proven](#-what-is-proven)
  - [What is not verified](#-what-is-not-verified)
  - [Running verification](#️-running-verification)
- [Layout](#-layout)

## 🎯 Scope and non-goals

**✅ What this library is:**
- A C code emitter — walks a `gremlind_resolve_context` and writes
  typedefs + encoders + reader-style decoders to a caller-provided
  [`gremlinc_writer`](include/gremlinc/writer.h).
- Arena-driven — every intermediate string (mangled names, snake-cased
  field identifiers, literal defaults) lives in the arena supplied by
  `gremlind`; no hidden `malloc`.
- Spec-faithful — identifiers go through a keyword-aware mangler +
  flat-join collision resolver, field defaults go through typed const
  conversions (both ACSL-contracted).

**❌ What this library is not:**
- A parser. That's [`gremlinp`](../parser/README.md).
- A descriptor pool. That's [`gremlind`](../descriptors/README.md).
- A wire runtime. That's [`gremlin`](../runtime/README.md) — generated
  code `#include`s it.
- A full protoc replacement. Oneof, groups, services, option
  resolution, and JSON / text-format are out of scope.

## 🪜 The pipeline

A typical codegen run runs these passes in order on a
`gremlind_resolve_context` after the descriptor pipeline has completed
(`build` → `link_imports` → `resolve_type_refs` → `topo_sort_*`):

| Stage | Reads | Writes | Purpose |
| --- | --- | --- | --- |
| 🏷️ `gremlinc_assign_c_names` | `messages[].scoped_name`, `enums[].scoped_name` | `<descriptor>.c_name` | Flat-join each scoped name (`foo.bar.A.B` → `foo_bar_A_B`), pass through the keyword mangler (`int` → `int_`) and flat-join collision resolver (`_1`, `_2`, …). Must run before any emit so field cross-references can read `target->c_name` directly. |
| 📤 `gremlinc_emit_message_forward` | `m->c_name` | writer | Emit `typedef struct M M;` + reader forward decls + `static inline` prototypes for every message. Must precede any body so mutual-recursive field types can be referenced. |
| ✍️ `gremlinc_emit_enum` / `gremlinc_emit_message` | full descriptor | writer | Walk the file's `enums[]` and `messages[]` in descriptor order, dispatching each field to the appropriate per-family emitter (see below). Output layout: typedef → `_size` → `_encode` → reader struct + `_reader_init` → one `_reader_get_<field>` per field. |

Each pass is idempotent after its prerequisites; the context carries
state, not the caller.

## 🧩 Per-family emit modules

Per-field codegen is split across one `.c` file per wire-shape family.
`emit_message.c` is the orchestrator — it walks a message's fields and
dispatches to the right family module based on the field's resolved
type and label. Each family implements four operations:

| Operation | Purpose |
| --- | --- |
| `emit_size_field` | Append the field's contribution to the running `M_size` accumulator. |
| `emit_encode_field` | Emit tag-write + payload-write into `M_encode`. |
| `emit_reader_arm` | Emit the `if (t.value == <tag>) { … }` arm inside `M_reader_init`. |
| `resolve_default` | Convert the field's `[default = X]` (or type-zero) to a C literal usable in init, reader getter fallback, and presence checks. |

| Module | Field shape |
| --- | --- |
| `emit_scalar.c` | `int32/64`, `uint32/64`, `sint32/64`, `fixed32/64`, `sfixed32/64`, `double`, `float`, `bool`. |
| `emit_bytes.c` | `string`, `bytes`. |
| `emit_enum_ref.c` | Fields typed as a named enum (VARINT wire, enum typedef on the C side). |
| `emit_msg_ref.c` | Fields typed as a named message (LEN_PREFIX wire, nested reader on the C side). |
| `emit_repeated_scalar.c` | `repeated` numeric / bool / enum — supports both packed and unpacked wire forms. |
| `emit_repeated_bytes.c` | `repeated string`, `repeated bytes`. |
| `emit_repeated_msg.c` | `repeated <Message>`. |
| `emit_map.c` | `map<K, V>` — each entry lowered to a synthetic LEN_PREFIX nested message with key @ field 1, value @ field 2. |

Adding a new family = new `.c` file with the four operations + one
dispatch branch in `emit_message.c`.

## 🔨 Building

Dependencies: a C99 compiler and CMake ≥ 3.10. The parser + descriptors
sources are linked in directly (not as separate static libs), so the
whole codegen stack is one build unit.

```bash
mkdir build && cd build
cmake ..
make
```

Artifacts:
- 📦 `libgremlinc.a` — the static library (also statically links the
  parser and descriptors sources).
- 🧪 `gremlinc_tests` — the test runner. Run with `./gremlinc_tests`.

## 🔬 Formal verification

`gremlinc` is **not fully verified end-to-end** — the emit modules are
imperative string-building passes whose correctness is tested by
round-tripping the generated code through `gcc -fsyntax-only` and the
runtime tests. What IS verified is the two pure, deterministic
components codegen output depends on for correctness: the
camelCase → snake_case converter (field names) and the proto-const →
typed-C value conversions (field defaults).

### ✅ What is proven

**235 / 235 goals + 66 / 66 smoke tests pass**, run by `make verify`:

| Group                  | Proved      | Smoke     | Notes                                          |
| ---------------------- | ----------- | --------- | ---------------------------------------------- |
| 🐍 `verify-snake-case`   |  93 /  93   | 34 / 34   | `gremlinc_to_snake_case` + helpers             |
| 🔢 `verify-const-convert`| 142 / 142   | 32 / 32   | `gremlinc_const_to_{int32,int64,uint32,uint64,float,double}` |

For the verified pieces, WP proves:

- 🛡️ **Memory safety** — no out-of-bounds reads, no null derefs, no
  integer overflow (via `-wp-rte`).
- 📏 **Output bounds** — the snake-case converter's output buffer is
  sized `2 * span_len + 1` and the contract proves the loop writes
  within it.
- ➡️ **Branch coverage** — every const converter returns one of
  `GREMLINP_OK`, `GREMLINP_ERROR_INVALID_FIELD_VALUE`, or
  `GREMLINP_ERROR_OVERFLOW`, with `value == 0` on any non-OK result.
- 🎯 **Range-check correctness** — int32/uint32 overflow guards are
  tied to `INT32_MIN`/`INT32_MAX`/`UINT32_MAX` in the postconditions.

`verify-const-convert` passes `-warn-special-float none` because
`[default = inf]` / `[default = nan]` are legal proto defaults — the
parser resolves those identifiers to IEEE-754 values, so a float
result that IS inf/NaN is a valid output, not an RTE.

### ❓ What is not verified

- 🏭 **The emit modules** (`emit_*.c`) — imperative string builders
  whose output is tested by `gremlinc_tests` round-tripping the
  generated code through `gcc -fsyntax-only` and the runtime.
- 🏷️ **The C-name mangler** (`gremlinc_name_scope_mangle`,
  `gremlinc_cname_for_type`, `gremlinc_is_c_keyword`) — tested by
  `tests/naming_test.c` against the 44 C11 keywords + collision cases.
- 🔣 **`gremlinc_const_to_bool`** — calls `memcmp` on the span, which
  requires libc stub contracts (initialization / danglingness) that
  WP cannot reliably discharge; tested by `tests/const_convert_test.c`.
- 🧪 **The tests** in `tests/` — regular C integration tests, not
  proofs.

### 🛠️ Running verification

You need Frama-C with the WP plugin plus Alt-Ergo and Z3. See the
[parser README](../parser/README.md#️-running-verification) for full
install instructions — the setup is identical.

From the build directory:

```bash
make verify               # both targets
make verify-snake-case    # snake_case converter only
make verify-const-convert # const → typed-C conversions only
```

📂 Proof results are cached in `<repo>/gremlinc/.wp-cache/`; a cold
cache run takes under a minute.

## 📁 Layout

```
gremlinc/
├── include/gremlinc/    — public headers
│   ├── lib.h            — umbrella header, #include this
│   ├── writer.h         — caller-provided char-buffer writer
│   ├── naming.h         — identifier mangling + snake-case converter
│   ├── const_convert.h  — proto const → typed C value conversions
│   └── emit.h           — per-type emit entry points
├── src/
│   ├── writer.c         — writer impl (owned / fixed-buffer variants)
│   ├── naming.c         — mangler + `gremlinc_to_snake_case` (verified)
│   ├── const_convert.c  — scalar / bytes / enum conversions (verified)
│   ├── emit_common.{c,h}— scalar descriptor table + write macros
│   ├── emit_enum.c      — enum typedef emission
│   ├── emit_message.c   — orchestrator: forward decls + struct layout + dispatch
│   ├── emit_scalar.c    — scalar-field family
│   ├── emit_bytes.c     — string / bytes family
│   ├── emit_enum_ref.c  — enum-reference family
│   ├── emit_msg_ref.c   — message-reference family
│   ├── emit_repeated_scalar.c — packed / unpacked repeated numerics + enums
│   ├── emit_repeated_bytes.c  — repeated string / bytes
│   ├── emit_repeated_msg.c    — repeated <Message>
│   ├── emit_map.c       — map<K, V> family
│   └── lib.c            — empty wrapper (kept for library linkage)
├── tests/               — integration + corpus tests (not verified)
├── VERSION              — single source of truth for the version
└── CMakeLists.txt       — build + verify targets
```

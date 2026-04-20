# 🦎 gremlinp — a formally verified protobuf syntax parser

![version](https://img.shields.io/badge/version-0.1.0--dev-orange)
![status](https://img.shields.io/badge/status-active%20development-yellow)
![language](https://img.shields.io/badge/C-C99-blue)
![verified](https://img.shields.io/badge/Frama--C%20WP-100%25-brightgreen)

> 🚧 **Active development.** The API, the grammar coverage, and the
> verification boundary are all still moving. Pin a commit if you depend on
> it.

`gremlinp` is a ⚡ zero-allocation protobuf 3 / proto editions parser written
in C99 and formally verified with 🔬 [Frama-C WP](https://www.frama-c.com/).

Every function in the parser ships with an ACSL contract and is discharged by
the Alt-Ergo / Z3 SMT provers. The parser never calls `malloc`/`free`; all
results are spans (`{const char *start; size_t length}`) into the caller's
buffer.

## 📑 Table of contents

- [Scope and non-goals](#-scope-and-non-goals)
- [Using the library](#-using-the-library)
- [Building](#-building)
- [Formal verification](#-formal-verification)
  - [Current status](#-current-status)
  - [What is proven](#-what-is-proven)
  - [What is admitted](#️-what-is-admitted-and-why)
  - [What is not verified](#-what-is-not-verified)
  - [Running verification](#️-running-verification)
- [Layout](#-layout)

## 🎯 Scope and non-goals

**✅ What this library is:**
- A syntax parser — turns a proto source string into a stream of spans and
  tagged-union entries that describe the structure.
- Heap-free — the caller owns the buffer, the parser never allocates.
- Verified — every parser function has an ACSL contract proven with WP.

**❌ What this library is not:**
- A semantic checker. It does not resolve type names, check reference
  consistency, or validate field-number uniqueness.
- An AST builder. Results are positional (spans + start/end indices) and are
  consumed via iterators; there is no tree structure to walk.
- A code generator. That belongs in a separate layer.

## 📚 Using the library

The public entry point is `#include "gremlinp/lib.h"`. The usual workflow is:

1. 📝 Load the proto source into a `char *` buffer you own.
2. 🪢 Wrap it in a `struct gremlinp_parser_buffer` with
   `gremlinp_parser_buffer_init`.
3. 🔁 Iterate top-level entries with `gremlinp_file_next_entry` until it
   reports an error (`GREMLINP_OK` means a new entry was consumed; any other
   error — including "nothing left to parse" — terminates the loop).
4. 🎁 Each iterator result is a tagged union: dispatch on `result.kind` and
   read the corresponding `result.u.<variant>` — names, spans and body bounds
   are already there, no re-parsing needed.

```c
#include "gremlinp/lib.h"
#include <stdio.h>

void parse_proto(char *proto_src) {
    struct gremlinp_parser_buffer buf;
    gremlinp_parser_buffer_init(&buf, proto_src, 0);

    for (;;) {
        struct gremlinp_file_entry_result fe = gremlinp_file_next_entry(&buf);
        if (fe.error != GREMLINP_OK) break;

        switch (fe.kind) {
        case GREMLINP_FILE_ENTRY_SYNTAX:
            printf("syntax %.*s\n",
                (int)fe.u.syntax.version_length, fe.u.syntax.version_start);
            break;
        case GREMLINP_FILE_ENTRY_PACKAGE:
            printf("package %.*s\n",
                (int)fe.u.package.name_length, fe.u.package.name_start);
            break;
        case GREMLINP_FILE_ENTRY_IMPORT:
            printf("import %.*s\n",
                (int)fe.u.import.path_length, fe.u.import.path_start);
            break;
        case GREMLINP_FILE_ENTRY_OPTION:
            printf("option %.*s\n",
                (int)fe.u.option.name_length, fe.u.option.name_start);
            break;
        case GREMLINP_FILE_ENTRY_MESSAGE:
            printf("message %.*s\n",
                (int)fe.u.message.name_length, fe.u.message.name_start);
            /* fe.u.message.body_start / body_end delimit the body — feed them
             * straight into gremlinp_message_next_entry on a sub-buffer. */
            break;
        /* ... edition, enum, service, extend ... */
        default: break;
        }
    }
}
```

### 🧪 A full worked example

`tests/integration_test.c` walks `tests/proto/proto3.proto` end-to-end
with the iterator API and asserts every field name, index, label, option,
oneof variant, map key/value type, nested message and nested enum. It is the
canonical example of how to use the library from start to finish.

## 🔨 Building

Dependencies: a C99 compiler and CMake ≥ 3.10.

From this directory:

```bash
mkdir build && cd build
cmake ..
make
```

Or from the [top-level repo root](../README.md) to build every sibling
project in one tree:

```bash
mkdir build && cd build
cmake ..
make gremlinp         # just the parser
```

Artifacts:
- 📦 `libgremlinp.a` — the static library.
- 🧪 `parser_tests` — the test runner. Run with `./parser_tests`.

## 🔬 Formal verification

### 📊 Current status

**All 7486 proof obligations pass ✅, plus 1486 smoke tests (no dead code, no
contradictory contracts) in three groups:**

| Group              | Proved      | Smoke       | Wall (fresh)¹ | Notes                               |
| ------------------ | ----------- | ----------- | ------------- | ----------------------------------- |
| 🧮 `verify-buffer`  | 409 / 409   | 80 / 80     | 0m30s         | Buffer primitives                   |
| 🔤 `verify-lexems`  | 1787 / 1787 | 377 / 377   | 4m02s         | Lexers, identifier, literal parsers |
| 🌲 `verify-syntax`  | 5290 / 5290 | 1029 / 1029 | 17m52s        | All entry parsers                   |
| **`make verify-parser`** | **7486 / 7486** | **1486 / 1486** | **22m12s** | All three groups in parallel   |

¹ Wall-clock with a cold `.wp-cache`. With cache, subsequent runs only re-prove
changed goals and finish in seconds. Measured with `-wp-par 4` on a developer
laptop.

**Smoke tests are mandatory.** Frama-C's `-wp-smoke-tests` is wired into
`WP_BASE_FLAGS` and detects unreachable code, contradictory contracts, and
poisoned axiomatic models. Any new contract that introduces dead code in an
unrelated function fails the build immediately.

⚡ Subsequent runs only re-prove changed goals thanks to `-wp-cache update`.

### ✅ What is proven

For every parser function, WP proves:

- 🛡️ **Memory safety** — no out-of-bounds reads, no null derefs, no integer
  overflow (via `-wp-rte` runtime-error guards on every access).
- ➡️ **Offset monotonicity** — `buf->offset` never moves backwards.
- ↩️ **Error recovery** — on any error return, `buf->offset` is restored to
  its pre-call value. This is a hard invariant that lets parsers be tried
  speculatively by `*_next_entry` iterators.
- 📐 **Grammar structure** — e.g. a successful `gremlinp_enum_parse` ends at
  `'}'`; a successful `gremlinp_field_parse` ends at `';'`.
- 🏷️ **Output shape** — e.g. when `parse_map_type` succeeds, both key and
  value spans are non-empty `valid_full_identifier`s.
- 🧷 **Iterator kind ↔ union agreement** — for every `*_next_entry`, the
  contract proves that when `kind == GREMLINP_X_ENTRY_Y` and `error == OK`,
  the matching `u.<y>.error` is also `OK`. Consumers can read the union
  variant directly without a redundant check.

### ⚠️ What is admitted (and why)

The trust base is `include/gremlinp/axioms.h`. Everything in that file is
admitted; everything in `include/gremlinp/lemmas.h` is checked. The split is
deliberate — an audit only needs to read `axioms.h`.

**1. Recursive logic function lemmas** — `count_newlines_step`,
`count_newlines_non_negative`, `count_newlines_bounded`
Properties of `count_newlines`, a recursive ACSL logic function used to
compute line numbers. They are trivially true by induction over `(to - from)`,
but Alt-Ergo and Z3 cannot do induction in reasonable time. An interactive
prover (Coq, Isabelle) could close them — we take them as axioms to keep
verification within the WP/SMT tool loop.

**2. Libc numeric parsing** — `strtoll_value_correct`, `strtoull_value_correct`,
`parsed_double_value`
We use libc `strtoll`/`strtoull`/`strtod` for integer and float literal
parsing. Their numeric semantics are not modelled by Frama-C's libc stubs; we
axiomatize that when the libc call reports success, the returned value is in
the documented range. The *string ranges* WP feeds those functions are still
checked (the `\separated` and `\valid_read` requirements are proved).

**3. `strncmp` byte-by-byte equality** — `strncmp_zero_iff_chars_equal`
Frama-C's libc declares `strncmp` as an opaque logic function; we add a
single axiom restating its ISO C semantics for the case we use it in
(comparing a buffer region against a fixed-length keyword with no interior
nulls). The axiom is local and the precondition matches our call sites.

**4. Thin libc/macro wrappers** — `gremlinp_strtod` / `gremlinp_strtoll` /
`gremlinp_strtoull`, `gremlinp_pos_infinity` / `gremlinp_neg_infinity` /
`gremlinp_quiet_nan`
Declared `extern` in `include/gremlinp/std.h` with stronger contracts than
upstream Frama-C provides (`assigns __fc_errno`), then defined in
`src/entries/std.c`, which is built into `libgremlinp.a` but **not** passed
to the verifier. WP sees only the stronger declarations. This works around
two upstream Frama-C limitations:
- The bundled `strtod` contract does not assign `__fc_errno`, which would
  otherwise make our `errno == ERANGE` overflow handling appear as dead code.
- WP does not recognise the `INFINITY` / `NAN` macros expanded by `<math.h>`
  ("Hide sub-term definition / Unexpected constant literal INFINITY"), and
  `-wp-rte` would otherwise generate undischargeable `is_nan_or_infinite`
  assertions on every helper that materialises a sentinel float.

These admitted facts are intentionally small and local — they do not hide bugs
in the parser itself.

### ❓ What is not verified

- 🧪 **The tests** in `tests/` — integration tests are regular C code and
  exist to sanity-check behaviour, not to carry proofs.
- 📜 **Faithfulness to the protobuf grammar** — WP proves the code matches
  its ACSL contract. It does not prove the contract is a correct encoding
  of the official protobuf/editions grammar. That correspondence is
  checked by the integration test against `tests/proto/proto3.proto`.
- 🧵 **Thread safety** — functions are pure and span-based, so multiple
  readers over the same `const char *` are fine in practice, but there is
  no formal multi-thread spec.

### 🛠️ Running verification

You need Frama-C with the WP plugin plus the Alt-Ergo and Z3 SMT solvers.

#### 📥 Installing dependencies (Linux)

```bash
# opam
sudo apt install opam                # or your distro's equivalent
opam init --bare --disable-sandboxing
opam switch create frama-c --empty

# Frama-C + WP + provers
eval $(opam env --switch=frama-c)
opam install frama-c alt-ergo

# Z3 is usually available as a system package
sudo apt install z3
```

Make sure `frama-c`, `alt-ergo`, and `z3` are all on your `PATH`. Easiest way
is to add this to your shell rc:

```bash
eval $(opam env --switch=frama-c)
```

#### ▶️ Running proofs

From the build directory:

```bash
make verify-parser    # 🎯 Runs all three groups (the parser umbrella)
make verify-buffer    # 🧮 Buffer primitives only
make verify-lexems    # 🔤 Lexers (includes buffer)
make verify-syntax    # 🌲 Entry parsers (includes lexems + buffer)
```

📂 Proof results are cached in `<repo>/parser/.wp-cache/`. The first full run
takes several minutes; subsequent runs only re-prove changed goals and finish
in seconds.

If you edit a function's contract or body and want to re-prove from scratch:

```bash
# from parser/build/
rm -rf ../.wp-cache && make verify-parser
```

## 📁 Layout

```
parser/
├── include/gremlinp/   — public headers
│   ├── axioms.h        — admitted trust base (predicates + axioms)
│   ├── lemmas.h        — checked lemmas (claimed and discharged by WP,
│   │                     as opposed to admitted in axioms.h)
│   └── std.h           — extern declarations for libc/macro wrappers
│                         with stronger contracts than upstream
├── src/entries/        — parser implementation, one file per grammar element
│   └── std.c           — bodies of the std.h wrappers; built into the
│                         library but NOT passed to frama-c (so WP only
│                         sees the declarations + their contracts)
├── src/lib.c           — empty wrapper (kept for library linkage)
├── tests/              — integration tests (not verified)
│   └── proto/          — golden .proto fixtures used by the tests
├── VERSION             — single source of truth for the version
└── CMakeLists.txt      — build + verify targets
```

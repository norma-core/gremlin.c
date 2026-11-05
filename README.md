# gremlin.c

`gremlin.c` is a C99 protobuf implementation with the compiler, code
generator, and wire runtime in this repository. It does not call
`protoc`, does not link third-party libraries, and generated code writes
to caller-owned buffers.

The project is still pre-1.0. APIs and generated header shapes may
change; pin a commit if you depend on it.

## What You Get

- `gremlinc-gen`: a CLI and CMake helper that turn `.proto` files into
  C headers.
- `gremlin.h`: a header-only protobuf wire runtime used by generated
  code.
- Lazy readers over caller-owned buffers. Strings, bytes, and nested
  messages are zero-copy views.
- Two-pass encoding: generated `_size()` computes the exact byte count,
  then generated `_encode()` fills a `struct gremlin_writer`.
- In-tree parser and descriptor resolver, so consumers need only a C99
  compiler and CMake.

Supported generated-field families include scalar numeric fields,
strings/bytes, enums, nested messages, repeated fields, packed repeated
fields, maps, and unknown-field skipping.

## Verification

Formal verification is a machine-checked proof about code behavior.
Instead of running examples and hoping they cover the interesting cases,
the C functions are annotated with ACSL contracts and Frama-C WP proves
that the implementation satisfies those contracts for all inputs allowed
by the preconditions.

This is useful for the protobuf wire runtime because the important bugs
are low-level and easy to miss with tests: out-of-bounds reads or
writes, offset arithmetic overflow, non-canonical varint bytes,
incorrect fixed32/fixed64 layout, or malformed input that should return
a defined error. Tests still exist, but the proof gives stronger
coverage for these small shared primitives than any practical test
corpus can.

The main proof target is [`verify-runtime`](runtime/CMakeLists.txt). It
runs Frama-C WP over [`runtime/include/gremlin.h`](runtime/include/gremlin.h)
through [`runtime/tests/verify.c`](runtime/tests/verify.c), which
references the static-inline functions so WP checks their bodies.

Current runtime proof: `748 / 748` WP obligations. The exact function
set is `RUNTIME_WP_FCTS` in [`runtime/CMakeLists.txt`](runtime/CMakeLists.txt):
writer setup, varint/fixed size helpers, `_encode_at` writers,
varint/fixed/bytes decoders, `gremlin_skip_data`, zigzag conversions,
and float/double bit casts.

Run from the build directory:

```bash
cmake --build . --target verify-runtime
```

Generated code is proved in `integration-test`, on fixtures generated
at build time: `verify-generated` covers the read side and
`verify-generated-write` the write side (`_size`, `_encode_at` and
the encoder of each field it calls, `_encode`, `_cached_size`, their
ghost helpers and lemmas). Both take
the function list from the generated header and need CMake 3.19 or
newer:

```bash
cmake -S integration-test -B build-it
cmake --build build-it --target verify-generated verify-generated-write
```

The read-side fixture has one field of every family, and callers built
on its readers: `10170 / 10170` WP obligations.

`verify-generated-write` proves that `_size` returns the message's
logic size and that `_encode_at` writes only `[off, off + _size)` and
returns its end, together with callers that build and encode messages.
Which bytes `_encode_at` writes is not proved. It covers messages built
from varint and fixed scalars other than `sint`, `float`, `double` and
a singular `bool`, bytes and strings without a `[default]`, enums, and
non-recursive message fields of the same file. A repeated message field
must be the message's only message field, and its element type holds no
message field. Messages past a size limit, set to what proves within
the timeout, and all others are generated as before, without write-side
contracts. Headers included together should come from one `gremlinc-gen`
run, which keeps their logic names apart.

The repository also contains other proofs, such as descriptor arena
allocation and selected code-generator helpers. Parser proofs are useful
for the compiler itself, but they are not the proof surface generated
protobuf code relies on at runtime.

## Quick Start

```cmake
include(FetchContent)
FetchContent_Declare(gremlin
    GIT_REPOSITORY https://github.com/norma-core/gremlin.c.git
    GIT_TAG        main)
FetchContent_MakeAvailable(gremlin)

gremlinc_generate(
    TARGET       my_protos
    IMPORTS_ROOT ${CMAKE_SOURCE_DIR}/protos
    PROTOS       foo.proto nested/bar.proto)

add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE my_protos)
```

`my_protos` is an INTERFACE target that puts the generated headers and
runtime include path on your executable. A complete consumer project is
in [`example/`](example/README.md).

From this repo:

```bash
mkdir build && cd build
cmake ..
make
ctest
```

## Runtime API Shape

Generated messages use plain C structs and inline functions:

```c
size_t need = example_Person_size(&person);
uint8_t *buf = malloc(need);

struct gremlin_writer w;
gremlin_writer_init(&w, buf, need);
example_Person_encode(&person, &w);

example_Person_reader r;
enum gremlin_error err = example_Person_reader_init(&r, buf, w.offset);
```

See [`example/main.c`](example/main.c) for a full encode/decode path.

## Benchmarks

gremlin.c against Google's C++ protobuf, upb (Google's C runtime), protobuf-c,
nanopb (C for embedded) and prost (Rust) on the same bytes, and on DeepNested
against gremlin.zig and gremlin_go. Apple M3 Pro, macOS 26.6, Apple clang 17;
the C and C++ programs `-O3 -march=native` with LTO, prost with LTO and
`target-cpu=native`, gremlin.zig ReleaseFast, Go as `go build` makes it;
protobuf 36.2, protobuf-c 1.5.2, nanopb 0.4.9.2, prost 0.14.4 on Rust 1.99.0,
Zig 0.15.2, Go 1.27.3.
Median ns/op of 5 rounds, each program run for about 30 s a round, in
rotating order; in brackets, how many times slower than gremlin.c. Spread is
the widest (max - min) / median between rounds.

### 🌳 DeepNested, five levels of nested and repeated messages, 1411 bytes

| Operation | gremlin.c | protobuf C++ | upb | protobuf-c | nanopb | prost |
|---|---|---|---|---|---|---|
| 🔨 Marshal | **523 ns** | 836 ns (1.6x) | 1,175 ns (2.2x) | 1,417 ns (2.7x) | 11,126 ns (21.3x) | 1,134 ns (2.2x) |
| 📖 Unmarshal | **32.1 ns** | 2,014 ns (62.7x) | 1,898 ns (59.1x) | 2,035 ns (63.4x) | 4,806 ns (149.7x) | 3,463 ns (107.9x) |
| 🎯 Lazy Read | **32.0 ns** | 2,023 ns (63.2x) | 1,893 ns (59.1x) | 2,031 ns (63.5x) | 4,817 ns (150.5x) | 3,468 ns (108.4x) |
| 🔍 Deep Access | **89.5 ns** | 2,020 ns (22.6x) | 1,895 ns (21.2x) | 2,030 ns (22.7x) | 4,780 ns (53.4x) | 3,482 ns (38.9x) |
| Spread | 4.2% | 4.0% | 2.6% | 1.7% | 3.2% | 1.6% |

gremlin.zig and gremlin_go are from the run before prost was added, with
that run's gremlin.c:

| Operation | gremlin.c | gremlin.zig | gremlin_go | protobuf-go |
|---|---|---|---|---|
| 🔨 Marshal | **532 ns** | 880 ns (1.7x) | 1,582 ns (3.0x) | 2,293 ns (4.3x) |
| 📖 Unmarshal | **32.0 ns** | 116 ns (3.6x) | 203 ns (6.3x) | 3,706 ns (115.8x) |
| 🎯 Lazy Read | **32.3 ns** | 117 ns (3.6x) | 219 ns (6.8x) | 3,735 ns (115.6x) |
| 🔍 Deep Access | **91.0 ns** | 267 ns (2.9x) | 659 ns (7.2x) | 3,753 ns (41.2x) |
| Spread | 4.5% | 6.9% | 2.3% | 2.4% |

### 📜 golden_message, protobuf's `TestAllTypes`, 537 bytes

| Operation | gremlin.c | protobuf C++ | upb | protobuf-c | nanopb | prost |
|---|---|---|---|---|---|---|
| 🔨 Marshal | **185 ns** | 201 ns (1.1x) | 679 ns (3.7x) | n/a | n/a | 514 ns (2.8x) |
| 📖 Unmarshal | **278 ns** | 1,132 ns (4.1x) | 1,300 ns (4.7x) | n/a | n/a | 1,464 ns (5.3x) |
| 🎯 Lazy Read | **277 ns** | 1,128 ns (4.1x) | 1,312 ns (4.7x) | n/a | n/a | 1,462 ns (5.3x) |
| 🔍 Deep Access | **281 ns** | 1,128 ns (4.0x) | 1,305 ns (4.6x) | n/a | n/a | 1,462 ns (5.2x) |
| Spread | 3.6% | 4.0% | 2.8% | n/a | n/a | 3.1% |

- 🔨 **Marshal**: `_size` and encode a built message (others: their serialize).
- 📖 **Unmarshal**: parse the bytes, touch no field.
- 🎯 **Lazy Read**: parse, then read three root fields.
- 🔍 **Deep Access**: parse, then follow one path down to the leaf.

What they show: gremlin.c reads lazily, so Deep Access touches 5 of
DeepNested's 38 messages while the others decode all 38. The speedups are
those of reading part of a message, not of decoding all of it. protobuf C++
also allocates on the heap while parsing (11 blocks per DeepNested here, 30
with libstdc++), prost 98 per DeepNested and 47 per golden_message, and
their timings include that. On golden_message gremlin.c marshals the
message built in code and the others the parsed file, and
gremlin.c writes its repeated scalars packed, so fewer bytes. protobuf-c and
nanopb cannot read golden_message, which has groups. gremlin.zig and
gremlin_go run their own programs on their own copy of DeepNested (1409
bytes); gremlin.zig's golden_message run overruns its buffer and is not
run, gremlin_go's does other work and is not shown (see `bench/README.md`).

```bash
brew install cmake pkgconf protobuf protobuf-c nanopb   # or apt, see bench/
bench/run.sh                                    # 30 s per program and round, 5 rounds
                                                # prost joins when cargo is on PATH
```

Setup, fairness and the allocation counts are in [`bench/README.md`](bench/README.md).

## Repository Layout

```text
parser/           protobuf syntax parser used by the compiler
descriptors/      descriptor pool and resolution pipeline
runtime/          verified header-only wire primitives
gremlinc/         C code generator library
gremlinc-gen/     CLI driver and gremlinc_generate() CMake helper
example/          standalone consumer example
integration-test/ generated-code tests and benchmark corpus
```

## Related

This is a C99 port of
[`gremlin.zig`](https://github.com/octopus-foundation/gremlin.zig) with
the same encode/decode model and benchmark corpus.

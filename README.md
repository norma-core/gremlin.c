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

Current runtime proof: `724 / 724` WP obligations. The exact function
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
`verify-generated-write` the write side (`_size`, `_encode_at`,
`_encode`, `_cached_size`, their ghost helpers and lemmas). Both take
the function list from the generated header and need CMake 3.19 or
newer:

```bash
cmake -S integration-test -B build-it
cmake --build build-it --target verify-generated verify-generated-write
```

The read-side fixture has one field of every family, and callers built
on its readers: `8314 / 8314` WP obligations.

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

`integration-test/benchmark.c` is a C port of the gremlin.zig benchmark
corpus. On the current development machine, the generated C path is
about 2x faster than the Zig reference on the aggregate benchmark sum.

Reproduce locally:

```bash
cmake -S integration-test -B integration-test/build
cmake --build integration-test/build --target benchmark
./integration-test/build/benchmark 20000000
```

Benchmark builds use `-O3 -DNDEBUG -march=native -flto`, so results are
machine- and compiler-dependent.

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

# Benchmarks

gremlin.c against Google's C++ protobuf, upb, protobuf-c and nanopb on the
same bytes. The results are in the top-level [README](../README.md#benchmarks).

## Quick Start

```bash
bench/run.sh                  # 30 s per benchmark and round, 5 rounds
bench/run.sh 60 9             # seconds, rounds
```

`run.sh` builds every benchmark with `-O3 -march=native` into `build-bench/`
(`BUILD_DIR` overrides it). A short calibration run sets each program's
iteration count so that one run of it takes the given time. It then runs the
programs once per round in rotating order, so none always runs first, and
prints markdown tables of the median ns/op over the rounds. The Spread row is
the widest (max - min) / median between rounds over the four operations: how
far one round can be from another. A benchmark that fails stops the run.

gremlin.zig and gremlin_go join when their checkouts are given:

```bash
GREMLIN_ZIG_DIR=../gremlin.zig ZIG=$(brew --prefix zig@0.15)/bin/zig \
GREMLIN_GO_DIR=../norma-core/shared/gremlin_go/bench \
bench/run.sh
```

They run their own benchmark programs (`integration-test/benchmark.zig`,
`cmd/benchmark`), with the same four operations and the same fields read,
but not the same program: each builds its own DeepNested (1409 bytes there,
1411 here); gremlin_go's and protobuf-go's Marshal return a new slice each
call; both time with their own clock and print whole nanoseconds, and Go
runs a GC after warm-up. So they get a table of their own, for DeepNested
only. gremlin.zig's golden_message marshal writes 481 bytes
into a 480-byte buffer from update i = 10 on, so `run.sh` builds a copy
of the checkout in the build directory with that run taken out of `main`;
the checkout itself is not touched. gremlin_go's golden_message run is
memory-safe but does other work (its marshal sets a constant, its Deep
Access reads the repeated fields instead of the imported message), so it
runs and is not shown. gremlin_go's program also measures protobuf-go. gremlin.zig
needs Zig 0.15.

## Prerequisites

CMake, protoc and the libraries to compare against.

```bash
brew install cmake protobuf protobuf-c nanopb                    # macOS
apt install cmake libprotobuf-dev protobuf-compiler \
    libprotobuf-c-dev protobuf-c-compiler nanopb                 # Debian, Ubuntu
```

A library that is not found is skipped and its column reads n/a. Ubuntu
24.04's protobuf (3.21) has no upb, so upb is n/a there. nanopb's allocator
is set at compile time, so CMake builds its runtime from source: fetched from
GitHub at the installed generator's version, or a checkout given as
`-DNANOPB_SOURCE_DIR=<dir>`; without either, nanopb is n/a.

## Benchmark Structure

Two messages:

1. **DeepNested** (`benchmark.proto`): five levels of nested and repeated
   messages, 1411 bytes. Written as protoc text in `deep_nested.txtpb` and
   encoded with `protoc --encode` at build time, so every library reads
   identical bytes.
2. **golden_message**: protobuf's `TestAllTypes` test data, 537 bytes,
   from `integration-test/binaries`.

Four operations on each:

1. **Marshal**: serialize a built message. gremlin.c runs `_size` and then
   `_encode`; the others' serialize computes the size as well. On
   `golden_message` every library changes the same five fields per
   iteration.
2. **Unmarshal**: parse the bytes, touch no field.
3. **Lazy Read**: parse, then read three root fields.
4. **Deep Access**: parse, then follow nested messages down to the leaf.

| file | library |
|---|---|
| `../integration-test/benchmark.c` | gremlin.c, the existing benchmark target |
| `bench_cpp.cc` | protobuf C++ |
| `bench_upb.c` | upb |
| `bench_protobuf_c.c` | protobuf-c |
| `bench_nanopb.c` | nanopb, with `benchmark.options` and `nanopb_alloc.h` |

## Fairness

What the numbers do and do not show:

- **Deep Access is partial access.** It follows one path down DeepNested,
  5 of its 38 messages. gremlin.c parses lazily and touches only those 5;
  the others decode all 38 up front. The speedups are those of reading part
  of a message, not of decoding all of it. The same holds, more so, for
  Unmarshal and Lazy Read.
- **Same build for all.** Every benchmark is `-O3 -march=native` with the
  LTO the gremlin target uses (`-flto=thin` under LLVM Clang, `-flto` under
  GCC and Apple clang). protobuf and upb are linked as installed, so
  their library code is built however the package was.
- **Same input bytes.** DeepNested comes from `deep_nested.txtpb` through
  protoc. gremlin.c builds the message in code and checks that its encoding
  is that file byte for byte; nanopb checks the same of its own encoding.
  Every C and C++ program checks that it parses its input before timing it.
- **Marshal.** Each program marshals the same values, the five fields it
  changes going round 0 to 1023, so the encoded size repeats every 1024
  iterations instead of growing. On
  golden_message gremlin.c encodes the message built in code, with the
  values of the file; the others encode the file as parsed, unknown fields
  dropped.
- **Warm-up.** Every program runs each operation 100 times before timing
  it, as gremlin.zig and gremlin_go do.
- **Allocation.** C++ and upb parse into an arena over a reused 64 KiB
  block, protobuf-c and nanopb into a bump allocator reset before every
  parse. C++ still allocates on the heap: DeepNested's strings longer than
  its short-string buffer (11 of them past libc++'s 22 bytes, 30 past
  libstdc++'s 15), and on Linux 9 blocks for golden_message. `bench/allocs.sh`
  counts heap allocations per operation:

  | per parse | gremlin.c | protobuf C++ | upb | protobuf-c | nanopb |
  |---|---|---|---|---|---|
  | DeepNested, macOS (libc++) | 0 | 11 | 0 | 0 | 0 |
  | DeepNested, Linux (libstdc++) | 0 | 30 | n/a | 0 | 0 |
  | golden_message, macOS | 0 | 0 | 0 | n/a | n/a |
  | golden_message, Linux | 0 | 9 | n/a | n/a | n/a |

  macOS 26.6 with protobuf 36.2; Linux is Ubuntu 24.04 with its protobuf
  3.21, which has no upb.

  Marshal allocates nothing on the heap in any of these five; gremlin_go's
  and protobuf-go's return a new slice per call. The C++ timings include those
  allocations, as they would in use.
- **nanopb** is set up as on a device: strings and scalar arrays inline with
  a fixed `max_size` / `max_count`, repeated messages through pointers.
  Fully static, DeepNested would be a ~100 KiB struct cleared on every
  decode. It sizes each nested message by encoding it before writing it.
- **Groups.** protobuf-c and nanopb reject the group wire types, and
  `golden_message` has groups, so both are n/a there; both try the file and
  skip it only when it fails to parse.
- **Packing.** gremlin.c writes a repeated scalar with two or more
  elements packed and protoc writes proto2 ones one by one, so on
  `golden_message` gremlin.c writes fewer bytes. Both parse to the same
  message.
- **Teardown.** C++ and upb destroy their arena every iteration, inside the
  timing, which frees C++'s heap strings; protobuf-c and nanopb reset their
  bump pointer, as on a device, with nothing to free.
- **Iterations.** A program runs all its operations at one iteration count,
  sized so the whole program takes about the given time; at 30 s its
  fastest operation (gremlin.c's Unmarshal) still runs over half a second a
  round.
- **gremlin.zig and gremlin_go** run their own programs with their own copy
  of DeepNested (1409 bytes), on the same operations; golden_message is
  left out for them (above). gremlin.zig's `build.zig` builds its benchmark
  and library ReleaseFast, Go is plain `go build`.

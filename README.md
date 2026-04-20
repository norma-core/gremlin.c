# 🧟 gremlin.c — protobuf in pure, formally verified C99

![version](https://img.shields.io/badge/version-0.1.0--dev-orange)
![status](https://img.shields.io/badge/status-active%20development-yellow)
![language](https://img.shields.io/badge/C-C99-blue)
![verified](https://img.shields.io/badge/Frama--C%20WP-8440%2F8440-brightgreen)

> 🚧 **Active development.** APIs, codegen output, and verification
> boundaries are all still moving. Pin a commit if you depend on it.

`gremlin.c` is a C99 port of
[**gremlin.zig**](https://github.com/octopus-foundation/gremlin.zig) —
a zero-dependency, zero-allocation protobuf implementation built
around the same ideas: parse once, emit tight codegen, keep the
runtime primitives small enough to verify formally. Same goals, same
encode / decode shape, same benchmark corpus — different language.

Everything beyond a C99 compiler and CMake ≥ 3.10 is in-tree: the
parser, descriptor pool, wire-format primitives, codegen, and the CLI
driver. No `protoc`, no libm link, no hidden `malloc` paths in the
generated code.

## 📑 Table of contents

- [Benchmarks](#-benchmarks)
- [Subprojects](#-subprojects)
- [Quick start](#-quick-start)
- [Formal verification](#-formal-verification)
- [Layout](#-layout)
- [Related projects](#-related-projects)

## 📊 Benchmarks

All numbers below come from `integration-test/benchmark.c` against the
same workload mix used by [`gremlin.zig`](https://github.com/octopus-foundation/gremlin.zig),
so the two implementations are directly comparable op-for-op.

### 🖥️ Test environment

| | |
| --- | --- |
| 💻 Laptop        | Framework 16 |
| 🔧 CPU            | AMD Ryzen AI 9 HX 370 w/ Radeon 890M |
| 🐧 OS             | Fedora Linux 43 (Workstation Edition) |
| 🧬 Kernel         | 6.19.11-200.fc43.x86_64 |
| 🛠️ GCC            | 15.2.1 (Red Hat 15.2.1-7) |
| ⚙️ Clang          | 21.1.8 (Fedora 21.1.8-4.fc43) |
| 🦎 Zig            | 0.15.2 (gremlin.zig reference build) |

### Table 1 — gremlin.c vs gremlin.zig (20M iterations, ns/op)

| Benchmark              |  C (GCC) | C (Clang) |      Zig | GCC vs Zig | Clang vs Zig |
| ---------------------- | -------: | --------: | -------: | ---------: | -----------: |
| DeepNested Marshal     |   229 ns |    220 ns |   566 ns |      x2.47 |        x2.57 |
| DeepNested Unmarshal   |    23 ns |     26 ns |    51 ns |      x2.22 |        x1.96 |
| DeepNested Lazy Read   |    24 ns |     26 ns |    51 ns |      x2.13 |        x1.96 |
| DeepNested Deep Access |    65 ns |     66 ns |   165 ns |      x2.54 |        x2.50 |
| Golden Marshal         |    86 ns |     82 ns |   151 ns |      x1.76 |        x1.84 |
| Golden Unmarshal       |   204 ns |    203 ns |   410 ns |      x2.01 |        x2.02 |
| Golden Lazy Read       |   206 ns |    205 ns |   406 ns |      x1.97 |        x1.98 |
| Golden Deep Access     |   211 ns |    217 ns |   423 ns |      x2.00 |        x1.95 |
| **Sum per-op**         | **1048 ns** | **1045 ns** | **2223 ns** | **x2.12** |   **x2.13** |

C builds cross the Zig reference by roughly **2×** on the aggregate
sum — GCC and Clang are within noise of each other on every workload.

### Table 2 — `perf stat` (1M iterations, whole-program totals, Clang vs Zig)

Independent confirmation from the hardware counters: the C build
executes about half the instructions and burns about half the cycles
of the Zig reference over an identical workload. Cache behaviour is a
tie; the x2 wall-clock delta is explained almost entirely by
instruction count + IPC.

| Metric         |         C (Clang) |              Zig | Zig vs Clang     |
| -------------- | ----------------: | ---------------: | ---------------- |
| Instructions   |    31,406,664,618 |   60,996,429,152 | x1.94 more       |
| Cycles         |     5,109,043,462 |   10,565,282,597 | x2.07 more       |
| Branch misses  |           781,518 |        1,516,035 | x1.94 more       |
| Cache misses   |            24,665 |           24,737 | x1.00 (tie)      |
| Wall time      |            1.05 s |           2.20 s | x2.10 slower     |
| IPC            |              6.15 |             5.77 | —                |

### 🔁 Reproducing

```bash
mkdir build && cd build
cmake ..
make benchmark
./integration-test/benchmark 20000000    # 20M iterations
```

The benchmark target is built at `-O3 -DNDEBUG -march=native -flto`.

## 🧩 Subprojects

Each directory is a self-contained CMake project; most also carry an
ACSL verification suite. Read the per-project README for the full
contract.

| Path | What it is | Verified |
| --- | --- | --- |
| 🦎 [`parser/`](parser/README.md) | `gremlinp` — zero-alloc protobuf 3 / editions syntax parser. Every function has an ACSL contract. | 7486 / 7486 goals |
| 🧬 [`descriptors/`](descriptors/README.md) | `gremlind` — descriptor pool: build → link imports → cycles → scoped names → visibility → extends → type-refs → topo sort. | arena: 115 / 115 goals |
| ⚡ [`runtime/`](runtime/README.md) | `gremlin` — header-only wire primitives (varint, fixed32/64, zigzag, LEN_PREFIX bytes). `#include`d by every generated header. | 719 / 719 goals |
| 🛠️ [`gremlinc/`](gremlinc/README.md) | The C code generator — walks a resolved descriptor pool and emits one `.pb.h` per input. Dispatches per wire family. | snake-case + const-convert: 235 / 235 goals |
| 🧾 [`gremlinc-gen/`](gremlinc-gen/README.md) | CLI wrapper around `gremlinc` + the `gremlinc_generate()` CMake helper that consumers call. | — (thin driver) |

## 🚀 Quick start

From the repo root:

```bash
mkdir build && cd build
cmake ..
make
ctest
```

That brings up every subproject, builds `libgremlinp.a`,
`libgremlind.a`, `libgremlinc.a`, the header-only runtime interface,
and the `gremlinc-gen` CLI, then runs every test suite.

### Generating code for your own `.proto` files

`gremlinc-gen/cmake/GremlincGenerate.cmake` is auto-loaded when the
top-level tree is brought in. Downstream projects do:

```cmake
add_subdirectory(path/to/gremlin.c)

gremlinc_generate(
    TARGET       my_protos
    IMPORTS_ROOT ${CMAKE_SOURCE_DIR}/protos
    PROTOS       foo.proto nested/bar.proto
)

add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE my_protos)
```

`my_protos` is an INTERFACE library that carries the generated
headers on its include path and transitively pulls in the runtime.
See [`gremlinc-gen/README.md`](gremlinc-gen/README.md) for the full
option surface.

## 🔬 Formal verification

Every subproject that ships verified code exposes a `verify-*` umbrella
target; the top-level build aggregates them behind a single `verify`
target.

```bash
cmake --build . --target verify               # run every WP target

# or per-project:
cmake --build . --target verify-parser        # 7486 goals
cmake --build . --target verify-descriptors   # arena: 115 goals
cmake --build . --target verify-runtime       # 719 goals
cmake --build . --target verify-gremlinc      # 235 goals
```

Dependencies: [Frama-C](https://www.frama-c.com/) with the WP plugin
plus Alt-Ergo and Z3. The [parser README](parser/README.md#️-running-verification)
has full install instructions — the other subprojects share the same
toolchain.

📂 Proofs are cached in each subproject's `.wp-cache/` so subsequent
runs only re-prove changed goals.

## 📁 Layout

```
gremlin.c/
├── parser/          — gremlinp: verified protobuf syntax parser
├── descriptors/     — gremlind: descriptor pool + resolution pipeline
├── runtime/         — gremlin: header-only wire primitives (verified)
├── gremlinc/        — the C code generator library
├── gremlinc-gen/    — CLI driver + GremlincGenerate.cmake
├── shared/          — cross-project headers (test framework, etc.)
├── integration-test/— end-to-end tests + benchmarks against the zig build
└── CMakeLists.txt   — top-level umbrella: brings up every subproject
```

## 🔗 Related projects

- 🧟 [**gremlin.zig**](https://github.com/octopus-foundation/gremlin.zig)
  — the reference Zig implementation. This repo is a C99 port with
  the same encode / decode shape and the same benchmark corpus
  (`integration-test/` cross-checks the two byte-for-byte against
  golden files). If you want the upstream docs, performance
  benchmarks, or a Zig-side integration guide, start there.

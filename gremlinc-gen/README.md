# 🧾 gremlinc-gen — protobuf C codegen CLI

![version](https://img.shields.io/badge/version-0.1.0--dev-orange)
![status](https://img.shields.io/badge/status-active%20development-yellow)
![language](https://img.shields.io/badge/C-C99-blue)

> 🚧 **Active development.** The CLI shape is minimal on purpose — it's
> likely to grow option flags (stdout mode, per-file include overrides,
> error-format switches) once real consumers start asking for them.

`gremlinc-gen` is the 📟 command-line driver around
[`gremlinc`](../gremlinc/README.md). Given a list of `.proto` files and
a root directory they live under, it runs the full descriptor + codegen
pipeline and writes one `<stem>.pb.h` per input. The generated headers
`#include` [`gremlin.h`](../runtime/README.md) for the wire-format
primitives.

It's the glue between a build system (CMake, Make, Bazel) and the
gremlinc library — the library does the work, this executable just
handles argv, filesystem I/O, and the pipeline stage sequencing.

## 📑 Table of contents

- [Scope and non-goals](#-scope-and-non-goals)
- [Usage](#-usage)
  - [Options](#️-options)
  - [Example](#-example)
- [Building](#-building)
- [Layout](#-layout)

## 🎯 Scope and non-goals

**✅ What this tool is:**
- A thin CLI wrapper around the gremlinc pipeline — reads proto
  sources, runs build / link / cycles / scoped-names / visibility /
  extends / type-refs / assign-c-names / emit, and writes one
  `<stem>.pb.h` per listed input.
- Subdirectory-preserving — `google/protobuf/any.proto` →
  `<out>/google/protobuf/any.pb.h`. Intermediate directories are
  `mkdir -p`'d as needed.
- Cross-file-aware — every listed file shares one name scope, so
  `message X` in `a.proto` can be referenced from `b.proto` as long
  as `a.proto` is also on the command line.

**❌ What this tool is not:**
- A dependency resolver. Every `.proto` file that participates in the
  compile must appear on the command line. Files referenced by
  `import "..."` that aren't also passed as arguments cause a
  `GREMLINP_ERROR_IMPORT_TARGET_NOT_FOUND` at the link stage —
  `protoc`-style `-I` search path discovery is not implemented on
  purpose, so the caller (typically a build system) stays in control
  of which files get compiled together.
- An incremental build tool. Every invocation re-parses + re-emits
  every listed file. CMake's `add_custom_command` handles
  build-graph dependency tracking; this tool doesn't second-guess it.
- A protoc plugin. It doesn't read `CodeGeneratorRequest` from stdin
  or speak the plugin protocol — it's a self-contained binary with
  its own arg parser.

## 📚 Usage

```
gremlinc-gen [-R <imports-root>] [-o <out-dir>] <proto>...
```

### ⚙️ Options

| Flag | Default | Purpose |
| --- | --- | --- |
| `-R <dir>`, `--imports-root <dir>` | `.` | Directory that `<proto>` paths and `import "..."` strings are resolved against. A positional `a/b.proto` is read as `<dir>/a/b.proto`, and its logical path (what the linker matches on) is `a/b.proto`. |
| `-o <dir>` | `.` | Output directory. One `<stem>.pb.h` is written per positional input, preserving its subdirectory structure under `<out-dir>`. |
| `-h`, `--help` | — | Print usage and exit. |

Every positional argument is both **an input to emit** and **a source
available for imports**. There's no separate import-only mode — if
file A imports B, both A and B must be listed, and both will get a
`.pb.h` emitted.

### 🚀 Example

```bash
gremlinc-gen -R protos -o gen \
    google/protobuf/any.proto \
    google/protobuf/timestamp.proto \
    myapp/user.proto
```

Reads `protos/google/protobuf/any.proto`, `protos/google/protobuf/timestamp.proto`,
and `protos/myapp/user.proto`; writes `gen/google/protobuf/any.pb.h`,
`gen/google/protobuf/timestamp.pb.h`, and `gen/myapp/user.pb.h`.
Cross-file type references (e.g. a field of type
`google.protobuf.Timestamp` inside `myapp.User`) resolve through the
shared name scope.

## 🔨 Building

Dependencies: a C99 compiler and CMake ≥ 3.10. The [gremlinc
library](../gremlinc/README.md) is pulled in via `add_subdirectory`,
so there's nothing to install first — the tool builds as a
single-target binary in the top-level CMake tree.

From this directory:

```bash
mkdir build && cd build
cmake ..
make
```

Or from the [top-level repo root](../README.md):

```bash
mkdir build && cd build
cmake ..
make gremlinc-gen
```

Artifact:
- 📟 `gremlinc-gen` — the CLI binary. No library artefacts are
  produced.

## 📁 Layout

```
gremlinc-gen/
├── src/
│   ├── main.c         — top-level orchestration: args → load → pipeline → emit
│   ├── args.{c,h}     — CLI parsing (-R, -o, -h)
│   ├── io.{c,h}       — slurp, output-path derivation, mkdir -p helper
│   ├── sources.{c,h}  — in-memory source list (path + buffer per file)
│   └── emit_file.{c,h}— per-file .pb.h body: header guard, enums, messages
├── VERSION            — single source of truth for the version
└── CMakeLists.txt     — builds `gremlinc-gen`, depends on gremlinc
```

# AGENTS.md

How to use gremlin.c from another project: generating headers, building
messages, encoding, reading. It records behaviour that is not obvious from a
single generated header, including where gremlin differs from protoc.

## Generating headers

```cmake
include(FetchContent)
FetchContent_Declare(gremlin
    GIT_REPOSITORY https://github.com/norma-core/gremlin.c.git
    GIT_TAG        <commit>)          # pre-1.0: pin a commit
FetchContent_MakeAvailable(gremlin)

gremlinc_generate(
    TARGET       my_protos
    IMPORTS_ROOT ${CMAKE_SOURCE_DIR}/protos
    PROTOS       foo.proto nested/bar.proto)

target_link_libraries(my_app PRIVATE my_protos)
```

- `my_protos` is an INTERFACE target: the generated include dir plus
  `gremlin_runtime` (`gremlin.h`). No library to link, no protoc needed.
- `OUT_DIR` is optional and defaults to `${CMAKE_CURRENT_BINARY_DIR}/<TARGET>`.
- List every proto the build uses, imports included. An import that is not
  listed fails with `Import target file not found`. Well-known types are not
  bundled: put `google/protobuf/*.proto` under `IMPORTS_ROOT` and list them.
- `nested/bar.proto` becomes `<OUT_DIR>/nested/bar.pb.h`, one header per
  proto, everything `static inline`.
- Generate every proto that ends up in one program in one
  `gremlinc_generate` call (one `gremlinc-gen` run). Collision suffixes,
  include guards and `extend` fields are resolved per run: two runs can both
  emit `Foo`, or two `dup.proto` from different roots share an include guard
  and the second header is silently skipped.

### Without CMake: the `gremlinc-gen` binary

`gremlinc_generate` only runs this binary. It can be built and run on its
own, for another build system, a script, or to look at the generated code:

```bash
cmake -S path/to/gremlin.c -B build
cmake --build build --target gremlinc-gen
build/gremlinc-gen/gremlinc-gen -R protos -o gen foo.proto nested/bar.proto
```

In a cross build (`CMAKE_CROSSCOMPILING`, `CMAKE_OSX_ARCHITECTURES` or `-A`
other than the host's) `gremlinc_generate` builds a second copy for the host
at `build/gremlinc-gen/host/bin/gremlinc-gen` and runs that one.
`-DGREMLINC_GEN_EXECUTABLE=<path>` runs a prebuilt host binary instead.

- `-R <dir>`: where protos and their imports are read from (default `.`).
  Each `<proto>` argument is read as `<dir>/<proto>`, and the argument as
  written is the path other files' `import "..."` must use.
- `-o <dir>`: where the `.pb.h` files go (default `.`); `nested/bar.proto`
  becomes `gen/nested/bar.pb.h`, directories created as needed.
- List every proto in one invocation, imports included; the rules above
  apply the same way.
- Exit status 1 with a message on any error (`no input files`, `Import
  target file not found`, `Type not found`, a parse error); nothing to
  check beyond that.
- The output is headers only. Compile them with `-I gen -I
  path/to/gremlin.c/runtime/include` (for `gremlin.h`); there is nothing
  to link.

Headers compile as C99 with `-Wall -Wextra -Werror -pedantic`.

## Names

- Package and nesting join with `_`, case kept: `package a.b; message Outer {
  message Inner {} }` gives `a_b_Outer` and `a_b_Outer_Inner`.
- Enum constants are `<EnumType>_<VALUE>`, nothing stripped: `a_b_Color_RED`.
- Field names become snake_case: `camelCase` is `camel_case`.
- A C keyword gets a trailing `_` (`int_`), as do `boolean` and `alignof`.
- A clash adds `_1`, `_2`, in the order types are named (enums first). This
  covers derived names too: with messages `Foo` and `Foo_size`, Foo's size
  function is `Foo_size_1`. A package prefix avoids most clashes.
- Types named after a name the generated contracts use (`len`, `src`, `i`,
  `integer`, `enum_wire`, ...) and types named `gremlin` or `gremlin_*` /
  `GREMLIN_*` also get `_1`. Field names `src`, `src_len`, `_has` and `_size`
  get `_1` inside their struct.
- Not handled: names starting with `__` or `_` and a capital.

Read the generated header for the exact names; do not guess them.

## Building and encoding a message

A message is a plain struct that borrows everything it points to:

| proto | C field |
|---|---|
| scalar | `int32_t`, `uint64_t`, `bool`, `double`, ... |
| enum | `<pkg>_<Enum>` |
| string, bytes | `struct gremlin_bytes { const uint8_t *data; size_t len; }` |
| message | `const Inner *inner` (NULL = absent) |
| repeated scalar | `T *x; size_t x_count;` |
| repeated string | `struct gremlin_bytes *x; size_t x_count;` |
| repeated message | `const Inner *const *x; size_t x_count;` |
| map | `<Msg>_<field>_entry *m; size_t m_count;`, entry `{ key; value; }` |

Every struct ends with `size_t _size`, a cache. Start from a designated
initializer or `= {0}`; there are no init functions. Data behind the pointers
is not copied and must live until encoding is done.

```c
size_t need = pkg_Msg_size(&m);       /* after the last change to m or any child */
uint8_t *buf = malloc(need);
struct gremlin_writer w;
gremlin_writer_init(&w, buf, need);
pkg_Msg_encode(&m, &w);               /* w.offset == need */
```

- **Call `_size` on the top-level message after every change and before
  every encode.** `_size` stores each message's size in its `_size`, and
  encode writes the length prefixes from that cache. Encoding after a change
  without `_size` writes wrong prefixes and can write past the buffer.
- **Nothing is bounds-checked at run time.** The buffer must hold `need`
  bytes from the offset; the guarantee is the proved contract, not a check.
  `_encode_at(&m, buf, off)` appends at `off` and returns the new offset.
- Message objects must be writable: `_size` writes `_size` through the
  `const` pointer, so a message in read-only memory faults.
- A child may be shared by several parents if `_size` runs on the parent
  right before it is encoded. Two threads encoding parents that share a child
  both write the child's `_size`.

What gets written:

- Fields go out in field-number order.
- Presence is the value: a scalar equal to zero (proto3) or to its
  `[default]` (proto2) is not written, nor an empty string or a NULL message.
  So proto3 `optional`, oneof members and proto2 `required` cannot carry an
  explicit zero, and `-0.0` is dropped.
- proto2 `= {0}` is not an empty message when fields have a non-zero
  `[default]`: those fields are written as 0. Set them to their defaults.
- oneof is not enforced. Members are plain fields; set one.
- Repeated scalars follow `[packed]` when it is given. Otherwise proto2 goes
  unpacked, as with protoc, and proto3 and editions go packed for two or more
  elements and unpacked for one, which is shorter. Valid protobuf, but for one
  element not protoc's bytes.
- Enums are written as int32: -1 takes 10 bytes, as with protoc.
- There is no slot for unknown fields; reading and writing again drops them.

## Reading a message

```c
pkg_Msg_reader r;
enum gremlin_error err = pkg_Msg_reader_init(&r, buf, len);
if (err != GREMLIN_OK) { /* r is unusable */ }

int32_t id = pkg_Msg_reader_get_id(&r);
struct gremlin_bytes name = pkg_Msg_reader_get_name(&r);   /* not NUL-terminated */

pkg_Inner_reader in;
err = pkg_Msg_reader_get_inner(&r, &in);                   /* parses inner now */

pkg_Msg_reader_items_iter it = pkg_Msg_reader_items_begin(&r);
for (size_t i = 0, n = pkg_Msg_reader_items_count(&r); i < n; i++) {
    pkg_Item_reader item;
    if ((err = pkg_Msg_reader_items_next(&it, &item)) != GREMLIN_OK) break;
}
```

- Readers are zero-copy: strings, bytes and nested messages point into
  `buf`. The buffer must outlive the reader, every sub-reader and iterator.
- `_reader_init` walks the top level once and caches scalars, spans and
  counts. It does not look inside nested messages, repeated message elements
  or map entries: their errors come later, from the getter or `_next`.
  Check those return values too.
- An absent field reads as 0, `{NULL, 0}`, or its proto2 `[default]`. An
  absent nested message gives a sub-reader that reads all defaults, with
  `GREMLIN_OK`.
- There are no `has_` functions and no oneof case. The reader's `_has`
  bitfield is internal.
- Maps iterate with `_next(&it, &key, &value)`; keys are not deduplicated.
- Scalar getters are O(1). A nested-message getter parses the submessage on
  every call: keep the sub-reader. `_next` scans forward to the next element.
- A reader is plain data: it can be copied and re-initialised.

Errors (`enum gremlin_error` in `gremlin.h`): `GREMLIN_ERROR_TRUNCATED`
(input ended mid-value), `_OVERFLOW` (varint too long), `_INVALID_WIRE_TYPE`,
`_INVALID_FIELD_NUM` (field 0).

Where reading differs from protoc:

- A field sent both packed and unpacked in one message is rejected with
  `GREMLIN_ERROR_INVALID_WIRE_TYPE`, on purpose.
- A singular nested message that appears twice is replaced by the last one,
  not merged.
- Several oneof members can be set at once.
- A known field with the wrong wire type is skipped as unknown.
- A missing `required` field is not reported, and strings are not checked
  for UTF-8.
- `uint32`, `sint32` and `bool` longer than 5 bytes give `_OVERFLOW`.
- Field numbers above 2^29-1 are not rejected.

## .proto support

| feature | gremlin.c |
|---|---|
| proto2, proto3 | yes |
| `[default = ...]` | yes |
| map, nested types, enums, negative enum values | yes |
| oneof | parsed, members are plain fields |
| proto3 `optional` | parsed, no presence when writing |
| `required` | parsed, not enforced |
| `[packed]` | yes when writing; either form read, not both in one message |
| groups | dropped from the struct, skipped when reading |
| `extend` | fields merged into the extended message, same run only |
| services, `json_name`, `deprecated`, `reserved` | ignored |
| custom field options `[(opt) = ...]` | parse error |
| aggregate option values `option x = { ... };` | parse error |
| editions | parse, feature semantics not applied |

## Working on gremlin.c itself

```bash
cmake -S . -B build && cmake --build build && (cd build && ctest)
cmake -S integration-test -B build-it && cmake --build build-it && (cd build-it && ctest)
```

The README's Verification section lists the Frama-C proof targets. A change
to the runtime or the generator is not done until they pass.

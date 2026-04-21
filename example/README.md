# 🚀 gremlin.c usage example

A minimal end-to-end walkthrough showing how a **third-party project
with no prior gremlin install** consumes `gremlin.c`:

1. **Write a `.proto`** — [`example.proto`](example.proto) defines a
   `Person` message with a nested `Address`, a `Role` enum, and a
   `repeated string` list of tags.
2. **Wire up CMake** — [`CMakeLists.txt`](CMakeLists.txt) pulls
   `gremlin.c` straight from GitHub with `FetchContent`, then calls
   the provided `gremlinc_generate()` helper to compile the proto
   into an `INTERFACE` library target. No pre-installed binary, no
   `protoc`, no submodules — `cmake ..` is enough.
3. **Use the generated API** — [`main.c`](main.c) populates a
   `Person`, encodes it into a caller-owned buffer, initialises a
   reader over the encoded bytes, and reads every field back. No
   heap allocations happen inside the runtime — the only `malloc`
   in this example is the encode buffer, which is entirely
   caller-controlled.

## 🔨 Build and run

From a fresh clone of your consumer project (nothing else installed):

```bash
mkdir build && cd build
cmake ..           # FetchContent clones gremlin.c from GitHub
make
./example
```

If you're hacking on gremlin.c itself and want the example to use a
local checkout instead of cloning, pass the source dir explicitly:

```bash
cmake .. -DGREMLIN_SOURCE_DIR=/path/to/gremlin.c
make
```

Expected output:

```
encoded 55 bytes

Person:
  name: "Ada Lovelace" (12 bytes)
  age:  36
  role: ADMIN
  address:
      city: "Berlin" (6 bytes)
      country: "Germany" (7 bytes)
    zip:  10115
  tags (2):
     : "admin" (5 bytes)
     : "member" (6 bytes)
```

## 📌 What to look at

- [`CMakeLists.txt`](CMakeLists.txt) — the three steps a real consumer
  copies into their own build: `add_subdirectory`, `gremlinc_generate`,
  `target_link_libraries`.
- [`main.c`](main.c) — the four stages (populate → size+encode →
  reader init → reader getters) annotated inline. Every field
  category in the proto (scalar, string, enum, nested message,
  repeated string) is exercised at least once.

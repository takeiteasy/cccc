# Headers Under Native Output

How `#include` behaves when `-c=native`, `-m` or `-c=generated` emit C for a host compiler.

## What gets replayed

CCCC re-emits the source's top-level `#include` lines into the generated C and forwards the flags a host compiler needs to find them (`-I` for the source's directory, the SDK search path).

| Mode | Includes replayed from |
|---|---|
| `-c=native`, `-m` | Every input file |
| `-c=generated` | The primary input only[^generated] |

| Situation | Result |
|---|---|
| `#include` inside a function body | Stays in place; never hoisted to file scope |
| User `-I`/`-isystem` directory that also holds CCCC's bundled headers | Demoted to `-idirafter`, so the host's real headers win |
| `--std=` | Only probes the flag spelling; output is a fixed GNU C11 floor ([native.md](native.md)) |
| Header reached through a chain of ordinary `#include`s | Treated as captured however long the chain |
| `extern inline __attribute__((gnu_inline))` function called from a user header | Also gets a weak out-of-line definition |
| Function an input declares before a replayed `#include`, called by that header's bodies | Prototyped ahead of the `#include` |

A comptime-generated declaration must use a type from a header that is captured into the output. `#include @shared` qualifies; `@comptime`, `@build` and `@test` do not, and `-c=generated` reports an error rather than write C nothing declares the type in. See [macros-global.md](macros-emit.md#what--cgenerated-captures).

### Single-header libraries

An included file that supplies function bodies (a `.c` amalgamation, or a header using an `#define FOO_IMPLEMENTATION` idiom) already provides them; nothing is serialized on top of the replayed `#include`. When several inputs include the same library, one line survives, placed so every input's configuration macro is in effect.[^impl]

## Headers that call back into your code

A header configured by macros can call your functions or statics from its bodies:

```c
static void my_log(const char *m) { /* ... */ }
#define LIB_LOG(s) my_log(s)
#include "lib_impl.h"      // bodies expand LIB_LOG(...)
```

The output prints a prototype for each such function or static just before the `#include`, with the types it names:

| Signature names | Printed ahead of the `#include` |
|---|---|
| Pointer to a tagged `struct`/`union` | `struct P;` |
| A `struct`/`union`/`enum`, or a typedef of one, by value | Its full definition |
| A typedef of your own (`typedef int my_t;`) | The typedef |
| A typedef an earlier replayed header supplies (`size_t`) | Nothing |
| `size_t` and siblings via another standard header | `#include <stddef.h>` |

A `static` whose signature names a type that cannot be declared yet (a type the replayed header itself declares, a function-local type, a `_BitInt` wider than 128 bits) is an error naming the function and header. A non-static one is left to the host compiler's diagnostic.

## URL includes

A URL `#include`/`#embed` needs a curl build ([install.md](install.md)) and `--allow-url` to download; cached URLs resolve without it. Downloads go to `--url-cache-dir` (default `$TMPDIR/.cccc`).

| Mode | What happens |
|---|---|
| `-c=native` | The cache directory is passed to the host compiler (`-idirafter`); a top-level URL include is rewritten to the cached absolute path |
| `-m`, `-c=generated` | The output starts with a comment naming the `-idirafter <cache-dir>` flag to add |
| `#embed` | Bytes are spliced in at parse time; the directive is dropped |

Saved output that includes a cached path only works while that cache entry exists, so it breaks across machines or after `--url-cache-clear`.

## Bundled headers

`cccc/kernel.h` (see [kernels.md](kernels.md)) and `cccc/opencl.h` ([opencl.md](opencl.md)) resolve with no `-I`. `reflection.h`, `testing.h` and `building.h` are injected implicitly for comptime, test and build files; `#include <reflection.h>` in user code does not resolve, and the public spelling is `<cccc/reflection.h>`.

[^generated]: Other inputs under `-c=generated` are comptime-support modules whose runtime code never reaches the output. Route one directive with `@emit` or `@shared` to opt it in.
[^impl]: Details, and the host-layout rule for self-hosting: [native-impl.md](native-impl.md#replayed-includes).

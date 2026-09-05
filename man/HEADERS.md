# Header Resolution

How CCCC finds the headers a `#include` refers to: CCCC's own bundled
copies of the standard headers, a project's own headers, and genuine SDK
headers under `--use-system-headers`.

## Quick start

```bash
# Standard headers resolve with zero configuration, from any directory:
cd /anywhere
/path/to/cccc program.c            # #include <stdbool.h>, <stdio.h>, etc. just work

# A project's own headers still need the usual -I:
cccc -I./myproject/include program.c

# Prefer real SDK headers for non-owned standard headers:
cccc --use-system-headers --sysroot "$(xcrun --show-sdk-path)" program.c
```

## Search order

For a standard header (one CCCC knows about — see "Owned vs. known" below),
`search_include_paths`/the `PP_INCLUDE` handler (`src/preprocess.c`) try, in
order:

1. **User `-I` paths.** An explicit `-I` always wins, including over CCCC's
   own bundled copy — this is what lets a developer point at an edited
   `include/*.h` without rebuilding (`tools/tests.py`'s runner always passes
   `-I./include` for exactly this reason).
2. **The embedded `src/std.c` table.** Every standard header's full text is
   compiled into the `cccc` binary itself, regenerated fresh on every build
   (`tools/regen_stdlib.sh`, from `tools/generate_stdlib.c`). This is tried
   with **no filesystem access at all** — no CWD dependency, no need for a
   `./include` directory to exist anywhere. This is what makes
   `#include <stdbool.h>` (and, via `<implicit-reflection.h>`'s own
   `#include <stdbool.h>`, every `[[cccc::comptime]]`-using file) resolve
   from any process CWD with zero flags.
3. **`./include` on disk**, CCCC's own bundled header directory, as a
   fallback. This only matters for a stage0 build (linked against
   `src/std_stub.c`, which embeds nothing — see `make bootstrap` in
   `BUILD.md`) and as a safety net if a build's embedded table is stale.
4. **System include paths** (`-i`/`--isystem`, `--sysroot`), gated by the
   rules below.

A non-standard header (a project's own `"local.h"`, or a header CCCC has
never heard of) skips steps 2–3 entirely and only ever resolves via `-I`
paths or system include paths.

## Owned vs. known standard headers

Two different classifications drive the rules above:

- **Known** (`get_std_header(name) != NULL`): CCCC has an embedded copy of
  this header at all. Roughly every C standard header plus a handful of
  POSIX ones.
- **Owned** (`is_compiler_owned_header` in `src/preprocess.c`): `stdarg.h`,
  `setjmp.h`, `stdbool.h`, `stddef.h`, `stdint.h`, `inttypes.h`,
  `complex.h`, `stdatomic.h`, `stdckdint.h`. These are tightly coupled to
  the VM's ABI or type system — `va_list`/`jmp_buf` layouts, the built-in
  boolean/integer types, `__cccc_*` builtin lowering for `creal`/`ckd_add`/
  `atomic_load`/etc. A genuine SDK copy of any of these either doesn't
  match CCCC's ABI or silently miscodegens. **Owned headers always resolve
  to CCCC's own copy, never to a system header, regardless of any flag** —
  there is no valid substitute.

## Flags

- **`--use-system-headers`**: for *known but not owned* standard headers,
  prefer a genuine SDK copy (searched via `-i`/`--sysroot`'s
  `system_include_paths`) before falling back to CCCC's own copy. Owned
  headers are unaffected — they still always resolve to CCCC's copy.
- **`--sysroot <path>`**: adds `<path>/usr/include` and
  `<path>/usr/local/include` to the system search paths; implies
  `--use-system-headers`.
- **`--no-builtin-includes`** (requires `--use-system-headers`): for known,
  non-owned standard headers, do **not** fall back to CCCC's own copy (step
  2/3 above) if the SDK copy isn't found — fail instead of silently
  substituting. Owned headers are exempt from this too, for the same
  reason: there's no SDK substitute to prefer in the first place, so
  "don't fall back" would just mean "never resolve `stdbool.h`", which
  isn't useful to anyone.
- **`CCCC_NATIVE_CC`** and friends (see `cccc_find_native_cc`) select the
  compiler `-c=native` shells out to; unrelated to header search but
  documented here since the interaction below is easy to miss.

### Pragma suppression in system-header mode

When `--use-system-headers` is active (or a file is marked as a system header
via `is_system_header`), CCCC suppresses:

- "unknown pragma ignored" — e.g. `#pragma GCC system_header`,
  `#pragma clang assume_nonnull begin/end`
- "unknown warning option" — e.g. Clang-specific `-W` names in
  `#pragma clang diagnostic ignored`

These are common in real SDK headers and are informational hints to the native
compiler that have no meaning in CCCC's VM execution.

## `-c=native` and auto-captured includes

Under `-c=native` (and `-m` / `-c=generated`), CCCC emits C and hands it to a
real host compiler, so header handling shifts:

- CCCC re-emits the source's own top-level `#include` lines verbatim into
  the generated C, and forwards the flags a host compiler needs to resolve
  them (`-I` for the source file's own directory, the real SDK search
  path). Under `-c=native` / `-m` this covers every command-line input,
  since the whole program is emitted. Under `-c=generated` only the
  **primary** input's directives are replayed: additional inputs there are
  comptime-support modules whose runtime code never reaches the output, so
  their `#include`/`#define` scaffolding would only be dead weight (and
  force an extra `-I` on the downstream `cc`). Route a directive with
  `@emit` or `@shared` to opt one in from such a file. Only a **top-level**
  `#include` is replayed this way: one written inside a function body is
  block-scoped in C, so it is never hoisted to file scope — its contents
  are parsed and serialized in place, inside the function that contains it,
  same as any other statement.
- Where a user `-I`/`-isystem` entry also contains CCCC's own bundled
  headers, it is demoted to `-idirafter` so the real host header always
  wins — CCCC's bundled copies are VM polyfills and must not shadow the
  real system headers in a native build.
- The `--std=` value is forwarded only as a flag-spelling probe; the
  emitted dialect is a fixed GNU C11 floor regardless (see
  [NATIVE.md](NATIVE.md)).
- A comptime-generated declaration's type must come from a header that is
  actually captured into the output — an `#include @shared`d header
  qualifies; one reached only via `#include @comptime`/`@build`/`@test`
  does not, and `-c=generated` reports a compile error rather than writing
  C nothing declares the type in. See
  [MACROS.md](MACROS.md#emit-directives-and-includes-in-generated-output).
- A header reached through a chain of captured, ordinary (non-bundled)
  `#include`s is treated as captured too, however many hops the chain
  runs — a user header's own `#include "helper.h"`, whose own
  `#include <nl_types.h>` in turn reaches a CCCC bundled header, is
  replayed as part of that chain's own text, so the host compiler reaches
  the real `nl_types.h` the same way it reaches every header that names
  it, and no bodiless declaration is needed anywhere along the chain. A
  bundled header reached through *another* bundled header (e.g. bundled
  `fcntl.h`'s own `#include "unistd.h"`) is the one case this does not
  cover: the replayed `#include <fcntl.h>` resolves to the *host's*
  `fcntl.h` under `-c=native`, whose own include graph CCCC cannot vouch
  for, so a bodiless declaration sourced from such a chain is still
  re-derived from CCCC's own header text rather than relied upon, the same
  as any other declaration this section describes. Two care points there:
  its declarator is parenthesized (`int (getc_unlocked)(FILE *stream);`),
  so a host header spelling the same name as a function-like macro (the
  unlocked-stdio family, or a `_FORTIFY_SOURCE` checked-function rewrite)
  doesn't expand it away; and a pointer parameter or return type that is
  itself a from_include typedef is spelled by that alias rather than
  decomposed to its underlying pointer type, so it still matches the real
  header's own declaration once both are visible to the host compiler.
- Any captured `#include` target that itself supplies a function's
  definition text -- a deliberate textual `.c` amalgamation (e.g.
  `#include "ops.c"`) or a vendored single-header **library** (a `.h` built
  via an `#define FOO_IMPLEMENTATION` idiom in exactly one TU) -- already
  supplies every function it defines to the output, external linkage
  included; none of them are re-serialized on top of the replayed
  `#include`. This holds even when the library's own public declarator
  names are macro-token-pasted (e.g. `#define DECORATE(name)
  prefix_##name`): the decision is keyed on where the function's body text
  is actually *written* (its macro-expansion site), not on the pasted
  token's own spelling location, which would otherwise misattribute it to
  wherever that macro happens to be defined. The one case this does NOT
  cover is a captured header's macro that only *produces* a function
  definition when invoked elsewhere -- a `#define DEFINE_THING(T) int
  thing_##T(void) { ... }` invoked in an ordinary command-line-input file --
  since the invocation site, not the header, is what actually needs to
  supply that body; the same expansion-site test correctly leaves this
  case alone. The same expansion-site rule governs a **bodiless
  declaration** and a **global** produced the same way: a captured header's
  macro that only *declares* a function, or that *defines* a global, when
  invoked elsewhere is likewise resolved by where it is invoked, not by
  where the producing macro happens to be written.

See [NATIVE.md](NATIVE.md) for the native pipeline itself.

## Private headers

`include/cccc/reflection.h`, `testing.h`, and `building.h` are CCCC's own
internal headers, injected implicitly for `[[cccc::comptime]]`-using files,
`[[cccc::test]]` test binaries, and `[[cccc::build]]` build scripts,
respectively (`tokenize_private_header` in `src/preprocess.c`). They are
embedded under their bare names (`"reflection.h"`, etc.) purely so that
internal lookup function can find them — an ordinary
`#include <reflection.h>` in user code deliberately does **not** resolve to
them; the public spelling, if you ever need it directly, is
`#include <cccc/reflection.h>`.

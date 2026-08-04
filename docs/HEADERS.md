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
   `BUILDING.md`) and as a safety net if a build's embedded table is stale.
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

## `-c=native` and auto-captured includes

`-c=native` emits C and hands it to a real host compiler (`cc`/`clang`/`gcc`
by default). Two things follow from that:

- CCCC's own bundled include directory is **never** forwarded to the native
  compiler (`run_native_backend` in `src/main.c` only forwards user-supplied
  `-I`/`-i`). If it were, the native compiler would see CCCC's polyfill
  headers (`typedef void FILE;`, etc.) fighting with the real system ones.
- CCCC auto-captures the primary source file's own top-level `#include`
  directives and re-emits them verbatim into the generated C
  (`preprocess.c`'s auto-capture; the include has to stay — e.g. `printf`
  is an FFI extern with no serialized prototype for the native compiler to
  see otherwise). A **quoted** include (`#include "local.h"`) that CCCC
  itself resolved relative to the primary file's own directory would
  otherwise be unresolvable to the native compiler, since the generated
  `.c` lives in a temp directory — so `run_native_backend` also forwards
  `-I<the primary file's own directory>` automatically.
- Because the real header is re-emitted via auto-capture, CCCC does **not**
  also re-emit type definitions it collected from that same header
  (`TypeNameRecord.from_include` in `src/cccc.h`, used by
  `cc_serialize_program`'s `!generated_only` path in `src/serialize.c`) —
  otherwise `typedef void FILE;` (from CCCC's own `stdio.h` polyfill) would
  collide with the real system stdio.h's `struct __sFILE`, and likewise for
  a header-defined aggregate like `struct tm` from `<time.h>`. Types
  synthesized by comptime/reflection code (no header of their own to
  collide with) are exempt and always re-emitted. This filter is itself
  skipped under `--emit-only`, since that flag turns auto-capture off — the
  header isn't re-emitted there, so the type definitions are still needed.

`-G`/`--emit-generated` (without `-c=native`) is a separate, unaffected
code path: its output is meant to be compiled *alongside* normal headers,
so it has never re-emitted header-sourced typedefs (see `generated_only` in
`serialize.c`).

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

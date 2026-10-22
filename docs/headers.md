# Headers

How `#include` is resolved: bundled headers, project headers and host headers.

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
   `include/*.h` without rebuilding (the test target always passes
   `-I./include` for exactly this reason).
2. **The embedded table (`src/std_table.c`).** Every standard header's full
   text is linked into the `cccc` binary itself (one row per header). This is tried
   with **no filesystem access at all** — no CWD dependency, no need for a
   `./include` directory to exist anywhere. This is what makes
   `#include <stdbool.h>` (and, via `<implicit-reflection.h>`'s own
   `#include <stdbool.h>`, every `[[cccc::comptime]]`-using file) resolve
   from any process CWD with zero flags.
3. **`./include` on disk**, CCCC's own bundled header directory, as a
   fallback for a header the table has no row for.
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
  `<path>/usr/local/include` to the system search paths, and
  `<path>/System/Library/Frameworks` to the system framework paths (see
  [Framework headers](#framework-headers-macos)); implies
  `--use-system-headers`.
- **`--no-builtin-includes`** (requires `--use-system-headers`; passing it
  alone is a CLI error): for known, non-owned standard headers, do **not**
  fall back to CCCC's own copy (step 2/3 above) if the SDK copy isn't found
  — fail instead of silently substituting. Owned headers are exempt from
  this too, for the same reason: there's no SDK substitute to prefer in the
  first place, so "don't fall back" would just mean "never resolve
  `stdbool.h`", which isn't useful to anyone.
- **`CCCC_NATIVE_CC`** and friends (see `cccc_find_native_cc`) select the
  compiler `-c=native` shells out to; unrelated to header search but
  documented here since the interaction below is easy to miss.

### Real SDK headers under `--use-system-headers`/`--sysroot`

A real SDK header's own `#if` guards key on compiler predefines, and can use
constructs CCCC's preprocessor needs help with.

**Predefined target macros.** CCCC defines the gcc/clang ABI macros SDK
headers test, with values from its own type model and bundled headers:

| Group | Examples |
|---|---|
| Endianness | `__BYTE_ORDER__`, `__ORDER_LITTLE_ENDIAN__`, `__LITTLE_ENDIAN__` (Apple only) |
| Integer types | `__INT64_TYPE__`, `__INT64_MAX__`, `__INT64_FMTd__`, `__SIZE_MAX__`, `__WCHAR_TYPE__` |
| Floating point | `__DBL_EPSILON__`, `__LDBL_MANT_DIG__`, `__FLT_RADIX__` |
| Data model | `_LP64`, `__CHAR_BIT__`, `__POINTER_WIDTH__`, `__BITINT_MAXWIDTH__` |
| Apple | `__MACH__`, `__APPLE_CC__`, `__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__` |

Each one equals its bundled-header counterpart (`__INT_MAX__ == INT_MAX`,
`__LDBL_MANT_DIG__ == LDBL_MANT_DIG`), so `__INT64_TYPE__` is `long` and
`long double` is double-precision. A command-line `-D`/`-U` overrides any of
them.

Identity and feature macros stay undefined because CCCC does not implement
what they advertise: `__clang__`, `__BLOCKS__`, `__ARM_NEON`, the
`__FLT16_*` macros (`_Float16` is storage-only), the `__GCC_ATOMIC_*` family, `__STDC_NO_THREADS__`. They are
not affected by `--compiler-family`.

Other constructs:

- **`defined`/`__has_*` reached through a macro expansion.** A macro can
  itself expand *to* `defined(...)` or `__has_builtin(...)`/
  `__has_attribute(...)`/etc — e.g. macOS's `<pthread.h>` wraps
  `defined(__DRIVERKIT_VERSION_MIN_REQUIRED)` inside its own
  `_PTHREAD_SWIFT_IMPORTER_NULLABILITY_COMPAT()` compat macro, and
  `<secure/_string.h>`'s `__is_modern_darwin()` does the same. CCCC's `#if`
  evaluator (`src/preprocess.c`) recognizes these operators wherever they
  end up in the expression, not just where they're written literally, so
  `#include <pthread.h>` under `--sysroot`/`--use-system-headers` resolves
  cleanly.
- **An `__has_*`-shaped operator CCCC doesn't recognize** (real SDK headers
  use `__has_declspec_attribute`/`__has_warning` —
  see [attributes.md](attributes.md#feature-test-preprocessor-operators))
  evaluates to `0` and emits a `-Wcpp` warning naming it, whether it's
  written literally or reached through a macro expansion, rather than the
  confusing "not a function" error a raw, unhandled `__has_foo(...)` would
  otherwise produce.
- **A host header named like a cccc-supplied one** (the SDK's real
  `Availability.h` under `--sysroot`) is replayed like any other host
  header. Only a file whose bytes equal cccc's bundled copy is treated as
  cccc's own, so `#include <stdio.h>` under `-c=native` stays a plain
  `#include <stdio.h>` instead of being flattened into the SDK's internal
  headers.
- **`secure/_string.h`'s `_FORTIFY_SOURCE` macros still need
  `-D_FORTIFY_SOURCE=0`.** Apple's fortified `strcpy`/`memcpy`/etc rewrite to
  `__builtin___*_chk(...)` calls CCCC doesn't implement — pass
  `-D_FORTIFY_SOURCE=0` to skip that header section entirely when using a
  real SDK's `<string.h>`.
- **`-c=native` with a real `<unistd.h>`** can declare functions (e.g.
  `setattrlistat`/`getattrlistbulk`) whose parameters use `<stdint.h>`'s
  fixed-width names. `stdint.h` is one of CCCC's *owned* headers (its
  typedefs are VM-ABI-coupled — see "Owned vs. known" above), so a
  compensating `#include <stdint.h>` is added automatically when the
  emitted C references one of those names but nothing else in the TU
  already includes it.
- **`--sysroot` on a Debian/Ubuntu-style multiarch Linux distro** also adds
  `<path>/usr/include/<arch>-linux-gnu` (derived from the running `cccc`
  binary's own architecture — there's no cross-`--sysroot` story today, so
  that's always right) alongside `<path>/usr/include`: a chunk of glibc's
  headers, notably `bits/wordsize.h`, live only under the triplet
  subdirectory. A bare `-i` stays literal and does not get this — supply the
  triplet directory explicitly if resolving real glibc headers by hand.
- **A declarator's trailing `__asm__("name")` (asm-label) followed by a
  `__attribute__(...)`** — real glibc's `<sys/cdefs.h>` `__REDIRECT_NTH`
  macro expands to exactly this order (`name proto __asm__ ("realname")
  __THROW`), the reverse of the common attribute-then-asm-label case —
  parses correctly; `declarator()` (`src/parse_types.c`) loops
  attribute-list/asm-label parsing until a pass consumes nothing, so any
  interleaving of the two is accepted.
- **`va_list`** is one type whichever of `<stdio.h>` and `<stdarg.h>` comes
  first: `__builtin_va_list` and `__gnuc_va_list` name cccc's own
  `struct __cccc_va_list`, so the SDK's `typedef __darwin_va_list va_list`
  and the `v*printf` prototypes agree with `va_arg`.
- **Legacy BSD calls** (`bcmp`, `bzero`, `index`, `getpagesize`, ...) are
  hidden by the SDK under cccc's predefined `_POSIX_C_SOURCE=200809L`;
  declare them yourself or `-U_POSIX_C_SOURCE`.

### Pragma suppression in system-header mode

When `--use-system-headers` is active (or a file is marked as a system header
via `is_system_header`), CCCC suppresses:

- "unknown pragma ignored" — e.g. `#pragma GCC system_header`,
  `#pragma clang assume_nonnull begin/end`
- "unknown warning option" — e.g. Clang-specific `-W` names in
  `#pragma clang diagnostic ignored`

These are common in real SDK headers and are informational hints to the native
compiler that have no meaning in CCCC's VM execution.

## Framework headers (macOS)

`<Name/X.h>` resolves to `Name.framework/Headers/X.h` (then
`Name.framework/PrivateHeaders/X.h`) under the framework search paths:

| Flag | Directory is treated as |
|------|-------------------------|
| `-F <dir>` / `--framework-path <dir>` | user framework path |
| `--iframework <dir>` | system framework path |
| `--sysroot <sdk>` | adds `<sdk>/System/Library/Frameworks` as a system framework path |

```sh
cccc --sysroot "$(xcrun --show-sdk-path)" -E watcher.c    # <CoreServices/CoreServices.h> resolves
cccc -F ./vendor/Frameworks app.c                         # ./vendor/Frameworks/Foo.framework
```

Framework paths are searched after `-I` and `--isystem`. A header inside a
framework also finds that framework's bundled sub-frameworks
(`X.framework/Frameworks/Name.framework`), so `<CoreServices/CoreServices.h>`
reaches `<CarbonCore/CarbonCore.h>` and friends. `#include_next` and
`__has_include_next` continue in that same order, so an `-I` header can hand
off to a framework header and a framework header to the same framework in a
later directory. Under `-c=native` the flags are forwarded to the host
compiler as `-F` and `-iframework`.[^fw]

[^fw]: `-framework NAME` at link time is a separate option.

## Guide pages

| Page | Covers |
|---|---|
| [headers-native.md](headers-native.md) | Includes under `-c=native`, URL includes and private headers |

# Real SDK Headers

What to know when `--use-system-headers` or `--sysroot` pulls in the host's own headers.

## Predefined macros

CCCC defines the gcc/clang ABI macros SDK headers test, with values from its own type model, so each equals its bundled-header counterpart (`__INT_MAX__ == INT_MAX`). A command-line `-D`/`-U` overrides any of them.

| Group | Examples |
|---|---|
| Endianness | `__BYTE_ORDER__`, `__ORDER_LITTLE_ENDIAN__`, `__LITTLE_ENDIAN__` (Apple only) |
| Integer types | `__INT64_TYPE__`, `__INT64_MAX__`, `__INT64_FMTd__`, `__SIZE_MAX__`, `__WCHAR_TYPE__` |
| Floating point | `__DBL_EPSILON__`, `__LDBL_MANT_DIG__`, `__FLT_RADIX__` |
| Data model | `_LP64`, `__CHAR_BIT__`, `__POINTER_WIDTH__`, `__BITINT_MAXWIDTH__` |
| Apple | `__MACH__`, `__APPLE_CC__`, `__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__` |

Macros advertising what CCCC does not implement stay undefined, whatever `--compiler-family` says: `__clang__`, `__BLOCKS__`, `__ARM_NEON`, `__FLT16_*`, the `__GCC_ATOMIC_*` family, `__STDC_NO_THREADS__`.

## Quirks

| Situation | Behaviour |
|---|---|
| `defined(...)` or `__has_*(...)` produced by a macro expansion (macOS `<pthread.h>`, `<secure/_string.h>`) | Recognised wherever it lands in the `#if` expression |
| An unrecognised `__has_*` operator (`__has_declspec_attribute`, `__has_warning`) | Evaluates to 0 with a `-Wcpp` warning naming it ([attributes.md](attributes.md#feature-test-preprocessor-operators)) |
| Host header named like a CCCC one (SDK `Availability.h`) | Replayed as a host header; only a file whose bytes equal CCCC's copy counts as CCCC's own |
| Apple's fortified `strcpy`/`memcpy` (`__builtin___*_chk`) | Pass `-D_FORTIFY_SOURCE=0` |
| `-c=native` with a real `<unistd.h>` using fixed-width names | `#include <stdint.h>` is added automatically when nothing includes it |
| `--sysroot` on Debian/Ubuntu multiarch | Also adds `<path>/usr/include/<arch>-linux-gnu`. A bare `-i` does not: supply the triplet directory yourself |
| `__asm__("name")` before `__attribute__(...)` (glibc `__REDIRECT_NTH`) | Parsed in either order |
| `va_list` | One type whether `<stdio.h>` or `<stdarg.h>` comes first |
| Legacy BSD calls (`bcmp`, `bzero`, `index`, `getpagesize`) | Hidden under the predefined `_POSIX_C_SOURCE=200809L`; declare them or `-U_POSIX_C_SOURCE` |

## Pragmas

In system-header mode CCCC suppresses "unknown pragma ignored" (`#pragma GCC system_header`, `#pragma clang assume_nonnull`) and "unknown warning option" (clang-specific names in `#pragma clang diagnostic`). They are hints for the native compiler.

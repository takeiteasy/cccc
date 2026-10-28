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
| An unrecognised `__has_*` operator (`__has_declspec_attribute`, `__has_warning`) | Evaluates to 0 with a `-Wcpp` warning naming it ([attributes.md](attributes-feature-tests.md)) |
| Host header named like a CCCC one (SDK `Availability.h`) | Replayed as a host header; only a file whose bytes equal CCCC's copy counts as CCCC's own |
| Fortified `strcpy`/`memcpy`/`snprintf` (`__builtin___*_chk`, `__read_chk`) | Supported; see [Fortified headers](#fortified-headers) |
| `-c=native` with a real `<unistd.h>` using fixed-width names | `#include <stdint.h>` is added automatically when nothing includes it |
| `--sysroot` on Debian/Ubuntu multiarch | Also adds `<path>/usr/include/<arch>-linux-gnu`. A bare `-i` does not: supply the triplet directory yourself |
| `__asm__("name")` before `__attribute__(...)` (glibc `__REDIRECT_NTH`) | Parsed in either order |
| `va_list` | One type whether `<stdio.h>` or `<stdarg.h>` comes first |
| `-c=native` / `-m` output on Linux | Starts with `#define`s for the predefined feature-test macros (`_POSIX_C_SOURCE`, `_XOPEN_SOURCE`, `_DEFAULT_SOURCE`, ...) so the host compiler reads glibc's headers as CCCC did (`fd_set` is `fds_bits` under `_XOPEN_SOURCE`, `__fds_bits` without). `-U` removes one |
| SDK `__asm("_fopen$DARWIN_EXTSN")` labels, glibc `__isoc23_sscanf` redirects | Bind to the plain native (`fopen`, `sscanf`) in the VM |
| Legacy BSD calls (`bcmp`, `bzero`, `index`, `getpagesize`) | Hidden under the predefined `_POSIX_C_SOURCE=200809L`; declare them or `-U_POSIX_C_SOURCE` |

## Host numbering

CCCC's own headers number `_SC_*`, `_PC_*`, `_CS_*`, `LC_*`, `POLL*`, `SCHED_*` and `nl_item` values, and lay out `struct statfs` and the SysV IPC structs, independently of the host. When the real header declares the function, the program holds the host's values, and the call goes to the host unchanged:

| Call | Header | Binds to |
|---|---|---|
| `sysconf(_SC_PAGESIZE)` | CCCC's | canonical translation |
| `sysconf(_SC_PAGESIZE)` | the SDK's or glibc's | the host function |

A header that falls back to CCCC's copy keeps the canonical numbering.

## Fortified headers

Apple's SDK fortifies the string and stdio calls with `_FORTIFY_SOURCE` by default. glibc does so when the program is optimised: `-O1` or above (CCCC defines `__OPTIMIZE__` there, as GCC does) together with `-D_FORTIFY_SOURCE=2`.

```sh
cccc --sysroot / --use-system-headers -O1 -D_FORTIFY_SOURCE=2 -c=native prog.c
```

The fortified spellings `__builtin___memcpy_chk`, `__builtin___snprintf_chk` and the other `__builtin___*_chk` builtins are built in, and `__has_builtin` reports them. A call that exceeds a known object size aborts with `*** buffer overflow detected ***` (SIGABRT, exit 134), as libc's does. Under `-c=native` the host compiler applies its own fortify. With `--checked-pointers` the [library bounds](safety-checked-interfaces.md#standard-library-calls) apply to the fortified spellings as to the plain ones, and trap first (exit 255).[^fortify]

[^fortify]: A glibc `memcpy` wrapper run in the VM sees no object size for its own parameters, so without `--checked-pointers` an overflow there is not caught; the bound at the call site is. Every `__*_chk` entry point glibc 2.39's fortified headers reference is a VM native, covering the string, stdio, wide-character, `poll`, `syslog`, stdlib and unistd wrappers; a failed size check aborts like `__chk_fail`. `longjmp` needs no entry point of its own.

## Obstacks

glibc's `<obstack.h>` works in the VM (glibc only; macOS has no obstacks). The chunk allocator and deallocator can be `malloc`/`free` or any guest function, with or without the extra argument of `obstack_specify_allocation_with_arg`. `obstack_printf` and `obstack_vprintf` format with CCCC's own printf engine, and the fortified `__obstack_printf_chk` and `__obstack_vprintf_chk` are natives.

```c
#include <obstack.h>
#include <stdlib.h>
#define obstack_chunk_alloc malloc
#define obstack_chunk_free free

struct obstack o;
obstack_init(&o);
obstack_grow(&o, "hello", 5);
obstack_free(&o, NULL);
```

Assigning `obstack_alloc_failed_handler` sets the function called when a chunk allocation fails, and `obstack_exit_failure` the status the default handler exits with (`EXIT_FAILURE` at program start). The default handler prints `memory exhausted` and exits through the program's `atexit` handlers. A handler that returns falls back to the default. A handler that leaves with `longjmp` is not supported in the VM.

## Host globals

Under `--sysroot ... --use-system-headers` the SDK's `stdin`/`stdout`/`stderr` (`__stdinp`/`__stdoutp`/`__stderrp` on macOS), getopt's `optarg`/`optind`/`opterr`/`optopt` (and `optreset` on macOS) and `environ` are plain `extern` declarations. In a VM run they are the host's own objects, so `fputs("x", stdout)`, `getopt` and `environ[0]` behave as in a native build. Calls such as `write`, `socketpair` and `fopen` run too, and the SDK's `FD_ZERO`/`FD_SET`/`FD_ISSET` work on macOS.

```c
#include <getopt.h>
extern char **environ;
static int *where = &optind;
```

A program that defines one of these names uses its own definition. Embedders bind their own objects with `cc_register_cdata` ([vm.md](vm.md#binding-host-data)).

## Pragmas

In system-header mode CCCC suppresses "unknown pragma ignored" (`#pragma GCC system_header`, `#pragma clang assume_nonnull`) and "unknown warning option" (clang-specific names in `#pragma clang diagnostic`). They are hints for the native compiler.

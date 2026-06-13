# Standard Library and Built-in Functions

CCCC provides a built-in standard library (embedded via `src/std.c`) and
a subset of GCC's `__builtin_*` functions. The standard library headers
in `include/` are compiled into the binary; the builtins are lowered directly
in the compiler and do not require any header include or host libc linkage.

| Status | Meaning |
|---|---|
| ✓ | Fully supported |
| ~ | Partial — accepted but behaviour is incomplete or approximated |
| ✗ | Not supported |

---

## Built-in Functions

CCCC supports a subset of GCC's `__builtin_*` functions. These are parsed and
lowered directly in the compiler — they do not require any header include and
do not link against host libc. A catch-all ticket
([#215](https://todo.sr.ht/~takeiteasy/cccc/215)) tracks remaining GNU builtins
not yet implemented.

### Math Constants

| Builtin | Return type | Description |
|---------|-------------|-------------|
| `__builtin_huge_val()` | `double` | Positive infinity (double) |
| `__builtin_huge_valf()` | `float` | Positive infinity (float) |
| `__builtin_huge_vall()` | `long double` | Positive infinity (long double) |
| `__builtin_inf()` | `double` | Positive infinity |
| `__builtin_inff()` | `float` | Positive infinity (float) |
| `__builtin_nan(tag)` | `double` | NaN; `tag` is a string literal (ignored) |
| `__builtin_nanf(tag)` | `float` | NaN (float) |

### Math Predicates

These are lowered to equivalent arithmetic comparisons at parse time.

| Builtin | Description |
|---------|-------------|
| `__builtin_isnan(x)` | Non-zero if `x` is NaN |
| `__builtin_isinf(x)` | Non-zero if `x` is infinite |
| `__builtin_isfinite(x)` | Non-zero if `x` is finite (not NaN, not infinite) |
| `__builtin_signbit(x)` | Non-zero if `x` is negative |

### Compiler Introspection

| Builtin | Description |
|---------|-------------|
| `__builtin_constant_p(expr)` | `1` if `expr` is a compile-time constant, else `0` |
| `__builtin_types_compatible_p(t1, t2)` | `1` if types `t1` and `t2` are compatible |
| `__builtin_reg_class(type)` | `0` = integer/pointer, `1` = float, `2` = other |
| `__builtin_expect(expr, hint)` | Returns `expr`; `hint` is a branch-prediction hint (ignored) |
| `__builtin_offsetof(type, member)` | Compile-time offset of `member` within `type` |

### Memory and Control Flow

| Builtin | Description |
|---------|-------------|
| `__builtin_alloca(size)` | Dynamically allocate `size` bytes on the stack |
| `__builtin_frame_address(0)` | Returns the current frame's base pointer (level 0 only) |
| `__builtin_unreachable()` | Marks an unreachable code path; halts the VM if executed |

### Atomic Operations

| Builtin | Description |
|---------|-------------|
| `__builtin_compare_and_swap(addr, old, new)` | CAS; returns bool |
| `__builtin_atomic_exchange(addr, val)` | Atomic exchange; returns old value |

### Bit-Manipulation Builtins

These are implemented as VM opcodes. The `ll` variants operate on 64-bit
values; the non-`ll` variants operate on 32-bit (unsigned int) values.

Behaviour for zero input on `clz`/`ctz` is undefined (as in GCC). `ffs(0)`
returns `0` by definition.

| Builtin | Return type | Description |
|---------|-------------|-------------|
| `__builtin_clz(x)` | `int` | Count leading zeros (32-bit) |
| `__builtin_clzll(x)` | `int` | Count leading zeros (64-bit) |
| `__builtin_ctz(x)` | `int` | Count trailing zeros (32-bit) |
| `__builtin_ctzll(x)` | `int` | Count trailing zeros (64-bit) |
| `__builtin_popcount(x)` | `int` | Population count (number of set bits, 32-bit) |
| `__builtin_popcountll(x)` | `int` | Population count (64-bit) |
| `__builtin_parity(x)` | `int` | Parity: `1` if odd number of set bits, else `0` (32-bit) |
| `__builtin_parityll(x)` | `int` | Parity (64-bit) |
| `__builtin_ffs(x)` | `int` | Index (1-based) of lowest set bit; `0` if `x == 0` (32-bit) |
| `__builtin_ffsll(x)` | `int` | `ffs` for 64-bit values |
| `__builtin_bswap16(x)` | `unsigned short` | Byte-swap a 16-bit value |
| `__builtin_bswap32(x)` | `unsigned int` | Byte-swap a 32-bit value |
| `__builtin_bswap64(x)` | `unsigned long` | Byte-swap a 64-bit value |

Width-variant pairs (`clz`/`clzll`, `ctz`/`ctzll`, etc.) share a single VM
opcode (`CLZ`, `CTZ`, `FFS`) with a width operand. The byte-swap variants share
the `BSWAP` opcode.

### Checked Arithmetic Builtins

These perform the arithmetic and report whether the result overflowed.

```c
int __builtin_add_overflow(a, b, result_ptr)
int __builtin_sub_overflow(a, b, result_ptr)
int __builtin_mul_overflow(a, b, result_ptr)
```

- Computes `a OP b`.
- Stores the (possibly wrapped) result through `result_ptr`.
- Returns non-zero (true) if the result overflowed for the type of `*result_ptr`.
- The result type is determined by the type of `*result_ptr`.
- All standard integer widths (char through long long) and their unsigned
  variants are supported.
- Implemented via the `IOVFL` VM opcode.

**Example:**

```c
#include <limits.h>

int sz;
if (__builtin_mul_overflow(base->size, len, &sz))
    error("array size overflow");

long long r;
if (__builtin_mul_overflow(a, b, &r))
    handle_overflow();
```

---

## Standard Library

### C89 / C90

| Header | Status | Notes |
|---|---|---|
| `<assert.h>` | ✓ | |
| `<ctype.h>` | ✓ | |
| `<errno.h>` | ✓ | |
| `<float.h>` | ✓ | |
| `<limits.h>` | ✓ | |
| `<locale.h>` | ✓ | Host locale APIs registered |
| `<math.h>` | ✓ | Full C99 function set registered |
| `<setjmp.h>` | ✓ | CCCC-specific implementation for VM calling convention |
| `<signal.h>` | ✓ | Full POSIX signal set (Darwin/macOS values); `signal` and `raise` are VM-managed — handlers are called synchronously from the dispatch loop, never from within a native signal context.  `SIGTRAP` with `-g` breaks into the debugger. |
| `<stdarg.h>` | ✓ | CCCC-specific implementation |
| `<stddef.h>` | ✓ | |
| `<stdio.h>` | ✓ | |
| `<stdlib.h>` | ✓ | |
| `<string.h>` | ✓ | |
| `<time.h>` | ✓ | |

### C99

| Header | Status | Notes |
|---|---|---|
| `<complex.h>` | ~ | Construction/projection macros and basic operations; complex function ABI is not supported |
| `<inttypes.h>` | ✓ | |
| `<stdbool.h>` | ✓ | |
| `<stdint.h>` | ✓ | |
| `<fenv.h>` | ✓ | Host floating-point environment APIs registered |
| `<tgmath.h>` | ~ | Type-generic macros for real floating types and complex absolute value |
| `<wchar.h>` / `<wctype.h>` | ~ | Common wide-character APIs registered |
| `<iso646.h>` | ✓ | |
| `snprintf`, `vsnprintf` | ✓ | |
| `strtof`, `strtold`, `strtoll`, `strtoull` | ✓ | |
| `llabs`, `lldiv` | ✓ | |

### C11

| Header | Status | Notes |
|---|---|---|
| `<stdalign.h>` | ✓ | |
| `<stdatomic.h>` | ~ | Header present; operations are non-atomic |
| `<stdnoreturn.h>` | ✓ | |
| `<threads.h>` | ✗ | CCCC is single-threaded |
| `<uchar.h>` | ✓ | `char8_t`, `char16_t`, `char32_t` defined; `mbrtoc16`/`c16rtomb`/`mbrtoc32`/`c32rtomb`/`mbrtoc8`/`c8rtomb` registered (native on glibc where available, shimmed via `mbrtowc`/`wcrtomb` elsewhere) |
| `aligned_alloc` | ✓ | Backed by host aligned allocation |
| `quick_exit` / `at_quick_exit` | ✓ | |
| `timespec_get` | ✓ | `TIME_UTC` |

### C17 / C18

C17 is a bug-fix release — no new language features or library functions were added. All C11 coverage figures apply.

| Change | Status | Notes |
|---|---|---|
| Removes `gets` | ✓ | `gets` is not registered in CCCC's stdlib |
| Deprecates `ATOMIC_VAR_INIT` | N/A | Atomics not supported |
| Clarifies undefined behaviour | N/A | Semantic, not syntactic |

### C23

| Header / Function | Status | Notes |
|---|---|---|
| `<stdbit.h>` | ~ | Core `stdc_leading_zeros`/`trailing_zeros`/`count_ones`/`bit_width`/`bit_floor`/`bit_ceil`/`has_single_bit` for `_ui`/`_ul`/`_ull`; `uint8_t`/`uint16_t` variants and `_Generic` dispatch macros tracked separately |
| `<stdckdint.h>` — checked integer arithmetic | ✓ | `ckd_add`/`ckd_sub`/`ckd_mul` via `__builtin_*_overflow` |
| `memset_explicit` | ✓ | |
| `memchr` | ✓ | |
| `memalignment` | ✓ | |
| `free_sized` / `free_aligned_sized` | ✓ | Conforming thin wrappers over `free` |
| `timegm` | ✓ | |
| `unreachable()` macro | ✓ | `<stddef.h>`, expands to `__builtin_unreachable()` |
| `strtol`/`strtoll`/`strtoul`/`strtoull` `0b`/`0B` binary prefix | ✓ | Accepted with base `0` or base `2` |
| `nullptr_t` (`<stddef.h>`) | ✓ | Defined as `typeof(nullptr)` |
| `bool`/`true`/`false` (`<stdbool.h>`) | ✓ | Real keywords in C23; `<stdbool.h>`'s macros are gated to pre-C23 modes, `__bool_true_false_are_defined` still set |
| `exp10`, `sinpi`/`cospi`/`tanpi`, `asinpi`/`acospi`/`atanpi`/`atan2pi` (+ `f`/`l`) | ~ | `double`/`long double` variants correct; `f` variants registered but affected by the float-FFI limitation ([#406](https://todo.sr.ht/~takeiteasy/cccc/406)) |
| `mbrtoc8`, `c8rtomb` (`<uchar.h>`) | ✓ | Full incremental state machine per §7.31.1 (one `char8_t` per call, `(size_t)-3` queued-byte convention) |

---

## POSIX

POSIX headers are embedded and backed by host OS calls. They are only available on POSIX targets (not Windows).

| Header | Status | Notes |
|---|---|---|
| `<arpa/inet.h>` | ✓ | Network byte-order conversion (`htonl`, `htons`, `ntohl`, `ntohs`), address manipulation (`inet_addr`, `inet_ntoa`, `inet_ntop`, `inet_pton`) |
| `<dirent.h>` | ✓ | Directory entry iteration (`opendir`, `readdir`, `closedir`, `DIR`, `struct dirent`) |
| `<dlfcn.h>` | ✓ | VM-managed dynamic loading (`dlopen`, `dlsym`, `dlclose`, `dlerror`); `dlsym` function symbols are callable through typed function pointers for scalar/pointer signatures |
| `<fcntl.h>` | ✓ | File control (`open`, `creat`, `fcntl`), `O_*` and `S_*` permission constants |
| `<fnmatch.h>` | ✓ | Filename pattern matching (`fnmatch`, `FNM_*` constants) |
| `<getopt.h>` | ✓ | Command-line option parsing (`getopt`, `getopt_long`, `optarg`, `optind`, `opterr`, `optopt`, `struct option`) |
| `<glob.h>` | ✓ | Pathname globbing (`glob`, `globfree`, `glob_t`, `GLOB_*` constants) |
| `<grp.h>` | ✓ | Group database (`getgrgid`, `getgrnam`, `struct group`) |
| `<libgen.h>` | ✓ | Pathname manipulation (`basename`, `dirname`) |
| `<netdb.h>` | ✓ | Network database (`gethostbyname`, `getaddrinfo`, `freeaddrinfo`, `struct hostent`, `struct addrinfo`) |
| `<netinet/in.h>` | ✓ | Internet address family (`struct sockaddr_in`, `struct in_addr`, `in_port_t`, `in_addr_t`, `INADDR_*`, `IPPROTO_*`) |
| `<poll.h>` | ✓ | Event polling (`poll`, `struct pollfd`, `nfds_t`, `POLL_*` constants) |
| `<pwd.h>` | ✓ | Password database (`getpwuid`, `getpwnam`, `struct passwd`) |
| `<regex.h>` | ✓ | Regular expression matching (`regcomp`, `regexec`, `regerror`, `regfree`, `regex_t`, `regmatch_t`) |
| `<strings.h>` | ✓ | BSD string functions (`strcasecmp`, `strncasecmp`, `bzero`, `bcopy`, `bcmp`, `index`, `rindex`) |
| `<sys/mman.h>` | ✓ | Memory management (`mmap`, `munmap`, `mprotect`, `msync`, `posix_madvise`), `PROT_*`, `MAP_*`, `MS_*`, `MADV_*` constants |
| `<sys/socket.h>` | ✓ | Socket API (`socket`, `bind`, `listen`, `accept`, `connect`, `setsockopt`, `getsockname`, `shutdown`, `struct sockaddr`, `socklen_t`) |
| `<sys/stat.h>` | ✓ | File status (`stat`, `fstat`, `lstat`, `chmod`, `mkdir`, `mkfifo`, `umask`), `struct stat`, `S_*` constants and macros |
| `<sys/time.h>` | ✓ | Time operations (`gettimeofday`, `settimeofday`), `struct timeval`, `struct timezone`, `timeradd`, `timersub`) |
| `<sys/types.h>` | ✓ | Basic system types (`dev_t`, `ino_t`, `mode_t`, `nlink_t`, `uid_t`, `gid_t`, `off_t`, `pid_t`, `blksize_t`, `blkcnt_t`, `useconds_t`, `sa_family_t`, `socklen_t`) |
| `<sys/wait.h>` | ✓ | Process wait (`wait`, `waitpid`), `WNOHANG`, `WUNTRACED`, `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`, `WIFSTOPPED`, `WSTOPSIG`, `WCOREDUMP` |
| `<termios.h>` | ✓ | Terminal I/O (`tcgetattr`, `tcsetattr`, `struct termios`, `cc_t`, `speed_t`, `tcflag_t`) |
| `<unistd.h>` | ✓ | Core POSIX API (`read`, `write`, `close`, `lseek`, `access`, `unlink`, `rmdir`, `chdir`, `getcwd`, `getpid`, `getppid`, `sleep`, `usleep`, `pipe`, `fork`, `execv`, `execve`, `execl`, `execlp`, `execle`, `execvp`, `_exit`, `ssize_t`, `STDIN/STDOUT/STDERR_FILENO`, `SEEK_*`, `F_*`/`R_*`/`W_*`/`X_OK`) |
| `<utime.h>` | ✓ | File time manipulation (`utime`, `struct utimbuf`) |

---

## Shim Implementations

Some C standard library functions are not available (or not correctly implemented) in the host libc on all supported platforms. CCCC provides software shims for these, registered in `src/stdlib/`. Platform guards (`#ifdef __APPLE__`, glibc version checks, etc.) should be used so that platforms with native support bypass the shim.

This table tracks shims that **reimplement** a standard function — not ABI-compatibility wrappers that only fix calling-convention issues. Remove a row when all supported platforms have a working native implementation.

| Function(s) | Source file | Reason for shim | Native availability | Removal condition |
|---|---|---|---|---|
| `memset_explicit` | `string.c` | C23 addition; not in older libcs | macOS 14+, glibc 2.39+ | When minimum supported SDK is raised past these versions |
| `aligned_alloc` | `stdlib.c` | macOS before 10.15 lacked `aligned_alloc`; shimmed via `posix_memalign` | macOS 10.15+, glibc 2.16+ | Already available on current macOS; shim is a safe no-op candidate |
| `free_sized`, `free_aligned_sized` | `stdlib.c` | C23 addition; no host libc exposes these yet | Nowhere yet | When host libc adds them |
| `memalignment` | `stdlib.c` | C23 addition; no host libc equivalent | Nowhere yet | When host libc adds it |
| `strtol`, `strtoll`, `strtoul`, `strtoull` | `stdlib.c` | C23 adds `0b`/`0B` binary prefix for base 0/2; not in host libc | Nowhere yet | When host libc C23 `strtol` is available |
| `mbrtoc16`, `c16rtomb` | `wide.c` | macOS lacks `<uchar.h>`; shimmed via `mbrtowc`/`wcrtomb` with `wchar_t` cast | glibc 2.16+, macOS absent | When macOS SDK adds `<uchar.h>` |
| `mbrtoc32`, `c32rtomb` | `wide.c` | Same as above; assumes UCS-4 = UTF-32 on all platforms | glibc 2.16+, macOS absent | When macOS SDK adds `<uchar.h>` |
| `mbrtoc8`, `c8rtomb` | `wide.c` | C23 `<uchar.h>` addition; absent on macOS, glibc < 2.36 | glibc 2.36+ | When macOS SDK adds `mbrtoc8`/`c8rtomb` |
| `exp10`, `exp10f`, `exp10l` | `math.c` | C23 addition; not declared in `<math.h>` on any platform | macOS: wraps private `__exp10`/`__exp10f` (10.9+), `exp10l` via `powl`; glibc: real libm symbols, declared manually | When a platform's `<math.h>` declares `exp10`/`exp10f`/`exp10l` directly |
| `sinpi`, `cospi`, `tanpi` (+ `f`/`l`), `asinpi`, `acospi`, `atanpi`, `atan2pi` (+ `f`/`l`) | `math.c` | C23 pi-trig family; no host libc exposes these | macOS: `sinpi`/`cospi`/`tanpi` (+`f`) wrap private `__sinpi`/`__cospi`/`__tanpi`; all `l` variants and the `asinpi`/`acospi`/`atanpi`/`atan2pi` family are portable shims (`asin(x)/CCCC_PI` etc., with exact integer/half-integer special-casing for `sinpi`/`cospi`/`tanpi`) | When a platform's `<math.h>` declares these directly |

> **Known limitation ([#406](https://todo.sr.ht/~takeiteasy/cccc/406)):** the native FFI call path does not support `float`-typed (single-precision) arguments/returns for *any* registered C function - this predates and is broader than this table. `exp10f`, `sinpif`, `cospif`, `tanpif`, `asinpif`, `acospif`, `atanpif`, `atan2pif` (and pre-existing functions like `sqrtf`, `sinf`, `fmodf`, `expf`, ...) are registered and implemented correctly, but currently return incorrect results when called. `double` and `long double` variants are unaffected.

### Pending shims (not yet implemented)

These functions require shims that have not been written yet. See the linked tickets.

| Function(s) | Ticket | Platform status | Notes |
|---|---|---|---|
| `printf`/`scanf` `%b`/`%B` specifier | [#394](https://todo.sr.ht/~takeiteasy/cccc/394) | macOS: absent (outputs literal `b`); glibc 2.35+ | Intercept format string in all `printf`/`scanf` family shims |

---

## Not Supported

| Feature | Notes |
|---|---|
| Threading (`<threads.h>`, `pthread`) | CCCC is single-threaded |
| Atomic operations (`<stdatomic.h>` operations) | Headers present; operations are non-atomic |
| Complex function call ABI | Passing or returning complex values by function call is not implemented |
| Full native ABI for runtime `dlsym` calls | Runtime dynamic function calls support scalar/pointer signatures through libffi using the current scalar/double metadata. Aggregate by-value arguments/returns, callbacks, variadic function-pointer calls, and full platform ABI descriptors are not implemented |
| Native code generation | CCCC produces VM bytecode only |
| Shared-library auto-linking for arbitrary undeclared symbols | `dlfcn.h` calls are available; `--library` opens requested libraries for registered FFI symbols |

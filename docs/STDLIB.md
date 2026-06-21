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
| `__builtin_infl()` | `long double` | Positive infinity (long double) |
| `__builtin_nan(tag)` | `double` | NaN; `tag` is a string literal (ignored) |
| `__builtin_nanf(tag)` | `float` | NaN (float) |
| `__builtin_nanl(tag)` | `long double` | NaN (long double) |

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
| `__builtin_alloca_with_align(size, align)` | Like `__builtin_alloca`; `align` is in bits and must be a constant. Only 16-byte alignment is guaranteed; finer alignment is accepted but not enforced. |
| `__builtin_frame_address(0)` | Returns the current frame's base pointer (level 0 only) |
| `__builtin_unreachable()` | Marks an unreachable code path; halts the VM if executed |

### String Builtins

These are forwarded to libc via the FFI and require the matching libc function to be available. They do not require `#include <string.h>` but are compatible with it (the `extern` declaration in `<string.h>` merges cleanly with the builtin registration).

| Builtin | Return type | Description |
|---------|-------------|-------------|
| `__builtin_strlen(s)` | `long` | Equivalent to `strlen(s)` |
| `__builtin_strcmp(a, b)` | `int` | Equivalent to `strcmp(a, b)` |

### Variadic Argument Macros (`__builtin_va_*`)

When `<stdarg.h>` is included, the following macros are available as aliases for the standard `va_*` macros:

| Macro | Equivalent |
|-------|------------|
| `__builtin_va_start(ap, last)` | `va_start(ap, last)` |
| `__builtin_va_end(ap)` | `va_end(ap)` |
| `__builtin_va_copy(d, s)` | `va_copy(d, s)` |
| `__builtin_va_arg(ap, type)` | `va_arg(ap, type)` |

These require `ap` to be of type `va_list` (the struct defined in `<stdarg.h>`). `__builtin_va_list` is defined as `char*` for macOS system-header compatibility and is a separate type.

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
| `<signal.h>` | ✓ | Full POSIX signal set (Darwin/macOS values); `signal` and `raise` are VM-managed. Handlers run from the dispatch loop, never native signal context. The macOS crash dispatcher preserves guest dispositions while trapping default `SIGSEGV`/`SIGBUS`/`SIGFPE`/`SIGILL`/`SIGABRT` into an interactive debugger. `SIGTRAP` with `-g` breaks into the debugger. |
| `<stdarg.h>` | ✓ | CCCC-specific implementation |
| `<stddef.h>` | ✓ | |
| `<stdio.h>` | ✓ | |
| `<stdlib.h>` | ✓ | |
| `<string.h>` | ✓ | `strchr`, `strrchr`, `strstr`, `strpbrk` are const-correct via `_Generic` dispatch macros: return type matches const-ness of the input pointer. `strpbrk` added in C23. `memchr` returns `void *` (no const dispatch — accepts any pointer type). |
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
| `<stdatomic.h>` | ~ | Header present; `atomic_fetch_add/sub/or/xor/and` and `atomic_load/store/exchange/compare_exchange` work correctly in single-threaded and thread-local contexts; cross-thread atomicity requires the GIL or an explicit mutex |
| `<stdnoreturn.h>` | ✓ | |
| `<threads.h>` | ✓ | Thread lifecycle (`thrd_create/join/exit/detach/yield/sleep/current/equal`), mutex (`mtx_init/lock/trylock/unlock/destroy`), condition variables (`cnd_init/wait/signal/broadcast/destroy`), and thread-specific storage (`tss_create/get/set/delete`); backed by host pthreads via POSIX `<pthread.h>` |
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
| `<stdbit.h>` | ✓ | All 14 operations (`leading/trailing zeros/ones`, `count ones/zeros`, `bit_width`, `has_single_bit`, `bit_floor`, `bit_ceil`, `first_leading/trailing one/zero`) for all 5 width suffixes (`_uc`/`_us`/`_ui`/`_ul`/`_ull`); `_Generic` dispatch macros; `__STDC_ENDIAN_*` macros |
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
| `printf`/`scanf` family `%b`/`%B` (binary integer) specifier | ✓ | macOS / glibc < 2.35: handled by the custom `format_printf.c`/`format_scanf.c` engines; glibc 2.35+ uses the native host implementation |

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
| `<pthread.h>` | ~ | POSIX pthread lifecycle, mutex, condition-variable, TLS key, and basic attr APIs are backed by host pthreads. VM bytecode execution is serialized by a recursive GIL, so pthreads provide correctness and blocking/wakeup semantics, not parallel VM execution. |
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
| `memset_explicit` | `string.c` | C23 addition not yet exposed consistently by supported host headers | Portable volatile-write shim on macOS and glibc | When supported host headers provide a portable native declaration |
| `aligned_alloc` | `stdlib.c` | macOS before 10.15 lacked `aligned_alloc`; shimmed via `posix_memalign` | macOS 10.15+, glibc 2.16+ | Already available on current macOS; shim is a safe no-op candidate |
| `free_sized`, `free_aligned_sized` | `stdlib.c` | C23 addition; no host libc exposes these yet | Nowhere yet | When host libc adds them |
| `memalignment` | `stdlib.c` | C23 addition; no host libc equivalent | Nowhere yet | When host libc adds it |
| `strtol`, `strtoll`, `strtoul`, `strtoull` | `stdlib.c` | C23 adds `0b`/`0B` binary prefix for base 0/2; not in host libc | Nowhere yet | When host libc C23 `strtol` is available |
| `mbrtoc16`, `c16rtomb` | `wide.c` | macOS lacks `<uchar.h>`; shimmed via `mbrtowc`/`wcrtomb` with `wchar_t` cast | glibc 2.16+, macOS absent | When macOS SDK adds `<uchar.h>` |
| `mbrtoc32`, `c32rtomb` | `wide.c` | Same as above; assumes UCS-4 = UTF-32 on all platforms | glibc 2.16+, macOS absent | When macOS SDK adds `<uchar.h>` |
| `mbrtoc8`, `c8rtomb` | `wide.c` | C23 `<uchar.h>` addition; absent on macOS, glibc < 2.36 | glibc 2.36+ | When macOS SDK adds `mbrtoc8`/`c8rtomb` |
| `exp10`, `exp10f`, `exp10l` | `math.c` | C23 addition; not declared in `<math.h>` on any platform | macOS: wraps private `__exp10`/`__exp10f` (10.9+); glibc: real libm symbols, declared manually; `exp10l` is a double-precision shim (see note below) | When a platform's `<math.h>` declares `exp10`/`exp10f`/`exp10l` directly |
| `sinpi`, `cospi`, `tanpi` (+ `f`/`l`), `asinpi`, `acospi`, `atanpi`, `atan2pi` (+ `f`/`l`) | `math.c` | C23 pi-trig family; no host libc exposes these | macOS: `sinpi`/`cospi`/`tanpi` (+`f`) wrap private `__sinpi`/`__cospi`/`__tanpi`; `double` variants use exact integer/half-integer special-casing; `l` variants are double-precision shims (see note below) | When a platform's `<math.h>` declares these directly |
| `printf`, `fprintf`, `sprintf`, `snprintf`, `vprintf`, `vfprintf`, `vsprintf`, `vsnprintf`, `scanf`, `fscanf`, `sscanf`, `vscanf`, `vfscanf`, `vsscanf` | `format_printf.c`, `format_scanf.c` (+ vendored `stb_sprintf.h`) | C23 `%b`/`%B` (binary integer) conversion specifier; host libc treats `%b` as unknown on macOS (`printf` prints literal `b`, `sscanf` fails to match) and on glibc < 2.35 | glibc 2.35+ | When the minimum supported macOS SDK / glibc version provides native `%b`/`%B` support everywhere |

> **Design note — `long double` math functions:** The VM models `long double` as
> 8 bytes (`__SIZEOF_LONG_DOUBLE__` = 8, matching macOS/x86-64 and the VM's
> internal precision). All `...l` math functions (e.g. `sqrtl`, `sinl`, `exp10l`)
> are therefore registered as double-precision bindings — either repointed to the
> corresponding `double` libc symbol or wrapped in a thin double-precision shim.
> This is correct for all existing tests and avoids a host-ABI mismatch on
> Linux/aarch64 where the native `long double` is 128-bit ([#491](https://todo.sr.ht/~takeiteasy/cccc/491)).

> **Known limitation ([#406](https://todo.sr.ht/~takeiteasy/cccc/406)):** the native FFI call path does not support `float`-typed (single-precision) arguments/returns for *any* registered C function - this predates and is broader than this table. `exp10f`, `sinpif`, `cospif`, `tanpif`, `asinpif`, `acospif`, `atanpif`, `atan2pif` (and pre-existing functions like `sqrtf`, `sinf`, `fmodf`, `expf`, ...) are registered and implemented correctly, but currently return incorrect results when called. `double` and `long double` variants are unaffected.

> **Known limitation ([#407](https://todo.sr.ht/~takeiteasy/cccc/407)):** when a user-defined variadic function forwards its `va_list` to `vprintf`/`vfprintf`/`vsprintf`/`vsnprintf`/`vscanf`/`vfscanf`/`vsscanf`, only the *first* variadic argument is passed through correctly; subsequent arguments are garbage. This is a pre-existing VM/FFI limitation, not specific to `%b`/`%B`.

# C Language Coverage

Conformance status for each C standard, plus the GNU built-in functions and
attribute syntaxes that JCC accepts. Intended as a reference for `--std` flag
work and as a checklist of what is currently parsed vs. what is semantically
honoured.

| Status | Meaning |
|---|---|
| ✓ | Fully supported |
| ~ | Partial — accepted by the compiler but behaviour is incomplete or approximated |
| ✗ | Not supported |

---

## C89 / C90

### Language

| Feature | Status | Notes |
|---|---|---|
| `char`, `short`, `int`, `long`, `float`, `double` | ✓ | |
| `unsigned` integer variants | ✓ | |
| `void` | ✓ | |
| Pointers — declaration, `*`, `&`, arithmetic | ✓ | |
| Arrays — fixed-size, multidimensional, initialisation | ✓ | |
| Structs — declaration, member access, nested | ✓ | |
| Unions | ✓ | |
| Enums — explicit values, use in expressions and switch | ✓ | |
| Bitfields — signed/unsigned, read-modify-write | ✓ | |
| `typedef` — including function pointer typedefs | ✓ | |
| All arithmetic, bitwise, comparison, logical operators | ✓ | |
| Assignment operators (`+=`, `<<=`, etc.) | ✓ | |
| Increment/decrement `++` `--` prefix and postfix | ✓ | |
| Ternary `? :` | ✓ | |
| Comma operator `,` | ✓ | |
| `sizeof` | ✓ | Compile-time for all types |
| Explicit cast expressions | ✓ | |
| `if` / `else` | ✓ | |
| `for`, `while`, `do`-`while` | ✓ | |
| `switch` / `case` / `default` | ✓ | |
| `break`, `continue`, `return` | ✓ | |
| `goto` and labels | ✓ | |
| Function declarations and definitions | ✓ | |
| Recursive functions | ✓ | |
| Function pointers — declaration, call, assignment | ✓ | |
| `extern` linkage | ✓ | |
| `static` — locals, globals, functions | ✓ | |
| `const` | ✓ | |
| `volatile` | ✓ | Tracked; prevents optimisation of accesses |
| `register` | ~ | Parsed and accepted; ignored |
| `auto` (storage class) | ~ | Parsed and accepted; ignored |
| String literals and concatenation | ✓ | |
| `L"..."` wide string literals | ✓ | Parsed; stored as UTF-32 |
| `L'...'` wide character literals | ✓ | |
| K&R-style function definitions | ✓ | |
| Trigraphs | ✗ | Removed in C23; intentionally not supported |
| `#include`, `#define`, `#undef` | ✓ | |
| `#ifdef`, `#ifndef`, `#if`, `#elif`, `#else`, `#endif` | ✓ | |
| `#error` | ✓ | |
| `#line` | ~ | Directive parsed; line tracking updated |
| Object-like and function-like macros | ✓ | |
| Macro stringification `#` and token-pasting `##` | ✓ | |
| Forward declarations and incomplete types | ✓ | |
| Multiple translation units / linker | ✓ | |

### Standard Library

| Header | Status | Notes |
|---|---|---|
| `<assert.h>` | ✓ | |
| `<ctype.h>` | ✓ | |
| `<errno.h>` | ✓ | |
| `<float.h>` | ✓ | |
| `<limits.h>` | ✓ | |
| `<locale.h>` | ✓ | Host locale APIs registered |
| `<math.h>` | ✓ | Full C99 function set registered |
| `<setjmp.h>` | ✓ | JCC-specific implementation for VM calling convention |
| `<signal.h>` | ✓ | Full POSIX signal set (Darwin/macOS values); `signal` and `raise` are VM-managed — handlers are called synchronously from the dispatch loop, never from within a native signal context.  `SIGTRAP` with `-g` breaks into the debugger. |
| `<stdarg.h>` | ✓ | JCC-specific implementation |
| `<stddef.h>` | ✓ | |
| `<stdio.h>` | ✓ | |
| `<stdlib.h>` | ✓ | |
| `<string.h>` | ✓ | |
| `<time.h>` | ✓ | |

---

## C99

Supported C99, C11, and C23 language features may also be accepted as
extensions in older `--std` modes. Enable `-Wpedantic` to diagnose those
pre-standard uses, or `-Werror=pedantic` to reject them.

### Language

| Feature | Status | Notes |
|---|---|---|
| `//` single-line comments | ✓ | |
| `long long int` and `unsigned long long int` | ✓ | |
| `_Bool` | ✓ | |
| `_Complex` | ✓ | Native scalar representation with arithmetic, casts, assignment, and equality |
| `_Imaginary` | ~ | Accepted as compatibility spelling for the corresponding complex type. Tracked on [#278](https://todo.sr.ht/~takeiteasy/jcc/278) and [#279](https://todo.sr.ht/~takeiteasy/jcc/279) |
| Mixed declarations and statements | ✓ | |
| Variable declaration in `for` initialiser | ✓ | |
| Variable-length arrays (VLA) | ✓ | Allocated via VM heap |
| Flexible array members (`struct { int n; int arr[]; }`) | ✓ | |
| Designated initialisers — structs and arrays | ✓ | |
| Compound literals | ✓ | |
| `inline` functions | ~ | Parsed; behaves like `static` — no inlining optimisation ([#205](https://todo.sr.ht/~takeiteasy/jcc/205)) |
| `restrict` pointers | ~ | Parsed and accepted; aliasing not tracked ([#262](https://todo.sr.ht/~takeiteasy/jcc/262)) |
| `static` array-parameter indices (`void f(int a[static 10])`) | ~ | Parsed and accepted; minimum-size constraint+not enforced |
| `__func__` predefined identifier | ✓ | |
| Variadic macros `__VA_ARGS__` | ✓ | |
`_Pragma(...)` operator | ✓ | |
| Hexadecimal floating-point literals (`0x1.8p+1`) | ✓ | |
| `u8"..."` UTF-8 string literals | ✓ | |
| `u"..."` UTF-16 string literals | ✓ | |
| `U"..."` UTF-32 string literals | ✓ | |
| `u'...'` and `U'...'` character literals | ✓ | |
| Universal character names `\uXXXX` / `\UXXXXXXXX` | ✓ | |
| Trailing comma in enumerator list | ✓ | |
| Integer constant expressions — stricter rules | ✓ | |

### Standard Library

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
| `__has_include` | ✗ | |

---

## C11

### Language

| Feature | Status | Notes |
|---|---|---|
| `_Generic` type-generic expressions | ✓ | |
| `_Alignof` | ✓ | |
| `_Alignas` | ✓ | |
| `_Static_assert` (and `static_assert` via `<assert.h>`) | ✓ | |
| `_Noreturn` | ~ | Parsed and accepted; no code-generation effect |
| `_Thread_local` | ~ | Parsed and accepted; no thread-local storage |
| `_Atomic` types | ~ | Parsed; non-atomic load/store emitted |
| Anonymous structs and unions | ✓ | |
| `char16_t` / `char32_t` types | ✓ | Provided by `<uchar.h>` |
| `u8`, `u`, `U` string and character literal prefixes | ✓ | See C99 row; support predates formal C11 adoption |

### Standard Library

| Header | Status | Notes |
|---|---|---|
| `<stdalign.h>` | ✓ | |
| `<stdatomic.h>` | ~ | Header present; operations are non-atomic |
| `<stdnoreturn.h>` | ✓ | |
| `<threads.h>` | ✗ | JCC is single-threaded |
| `<uchar.h>` | ~ | Types and basic conversion APIs |
| `aligned_alloc` | ✓ | Backed by host aligned allocation |
| `quick_exit` / `at_quick_exit` | ✓ | |
| `timespec_get` | ✓ | `TIME_UTC` |

---

## C17 / C18

C17 is a bug-fix release — no new language features or library functions were added. All C11 coverage figures apply.

| Change | Status | Notes |
|---|---|---|
| Removes `gets` | ✓ | `gets` is not registered in JCC's stdlib |
| Deprecates `ATOMIC_VAR_INIT` | N/A | Atomics not supported |
| Clarifies undefined behaviour | N/A | Semantic, not syntactic |

---

## C23

### Language

| Feature | Status | Notes |
|---|---|---|
| `typeof` / `typeof_unqual` | ✓ | |
| `constexpr` for objects | ~ | Parsed; constant propagation only — not a full compile-time guarantee |
| `auto` type inference | ✗ | `auto` is parsed as a storage class; C23 type-deduction form not supported |
| `nullptr` keyword | ✗ | |
| `_BitInt(N)` arbitrary-precision integers | ✗ | |
| Binary integer literals `0b10101010` | ✓ | |
| Digit separators `1'000'000` | ✓ | |
| `[[...]]` attributes | ~ | Parsed; `[[maybe_unused]]` and `[[deprecated]]` supported (see [Attributes](#attributes)) |
| `bool`, `true`, `false` as keywords (not just macros) | ~ | Available via `<stdbool.h>`; not reserved keywords at the tokeniser level |
| `u8` character literals (`u8'x'`) | ✗ | |
| Unnamed function parameters (`void f(int, double)`) | ✓ | |
| `static_assert` without message | ✗ | |
| Improved `enum` — underlying type and forward declaration | ✗ | |
| Decimal floating-point (`_Decimal32`, etc.) | ✗ | |
| `char8_t` | ✗ | |

### Preprocessor

| Feature | Status | Notes |
|---|---|---|
| `#elifdef` / `#elifndef` | ✓ | |
| `#warning` | ✓ | |
| `#embed` | ✓ | Supports `limit()`, `prefix()`, `suffix()`, `if_empty()`, `__has_embed()` |
| `__VA_OPT__` | ✓ | |
| `__has_c_attribute` | ✗ | |
| `__has_include` | ✗ | |

### Standard Library

| Header / Function | Status | Notes |
|---|---|---|
| `<stdbit.h>` | ✗ | |
| `<stdckdint.h>` — checked integer arithmetic | ✗ | |
| `memset_explicit` | ✗ | |
| `unreachable()` macro | ✗ | |

---

## GNU Extensions

| Feature | Status | Notes |
|---|---|---|
| Statement expressions `({ ... })` | ✓ | |
| `__attribute__((...))` | ~ | Parsed; `aligned`, `packed`, `unused`, `deprecated` supported (see [Attributes](#attributes)) |
| Labels as values `&&label` | ✓ | |
| Computed goto `goto *expr` | ✓ | |
| Switch case ranges `case 1 ... 5:` | ✓ | |
| Zero-length arrays `int arr[0]` | ✓ | |
| Nested functions | ✓ | Access to parent-scope variables via static link |
| `__builtin_*` | ✓ | Lowered by the compiler; see [Built-in Functions](#built-in-functions) for the full list |
| `__thread` storage class | ~ | Parsed; treated as `static` |
| `__restrict` / `__restrict__` | ~ | Parsed; aliasing not tracked ([#262](https://todo.sr.ht/~takeiteasy/jcc/262)) |
| `__typeof__` | ✓ | Synonym for `typeof` |
| `__asm__` / `asm(...)` inline assembly | ~ | Accepted; executed as a no-op unless a callback emits custom bytecode |

---

## Microsoft Extensions

A subset of MSVC's compiler extensions is recognised by JCC. The feasibility,
no-op policy, and the question of which extensions to support versus reject
are open design questions tracked in
[#289](https://todo.sr.ht/~takeiteasy/jcc/289). The table below mirrors the
groups from that ticket.

| Feature | Status | Notes |
|---|---|---|
| `__declspec(align(n))` | ✗ | Spelling alias of `__attribute__((aligned(n)))` — pending |
| `__declspec(deprecated)` | ✗ | Maps to existing `-Wdeprecated` — pending |
| `__declspec(dllimport)` / `dllexport` | ✗ | JCC module export — pending |
| `__declspec(naked)` | ✗ | No-op in the VM — pending |
| `__declspec(noalias)` / `noinline` / `inline` | ✗ | Hint-only — pending |
| `__declspec(noreturn)` / `nothrow` | ✗ | C11 equivalents exist — pending |
| `__declspec(restrict)` | ✗ | Lands with [#267](https://todo.sr.ht/~takeiteasy/jcc/267)–[#269](https://todo.sr.ht/~takeiteasy/jcc/269) restrict work |
| `__declspec(safebuffers)` / `selectany` / `code_seg` / `allocate` | ✗ | No-op shims — pending |
| `__declspec(thread)` | ✗ | TLS via VM per-thread storage — pending |
| `__cdecl` / `__stdcall` / `__fastcall` / `__thiscall` / `__vectorcall` | ✗ | Calling-convention keywords, no-op (JCC has a single VM ABI) — pending |
| `__ptr32` / `__ptr64` / `__sptr` / `__uptr` / `__unaligned` / `__w64` | ✗ | Pointer modifiers, no-op — pending |
| `__forceinline` / `__inline` | ✗ | Fold to existing `inline` — pending |
| `__assume(expr)` | ✗ | Optimizer hint — pending |
| `__noop` | ✗ | Variable-arg no-op builtin — pending |
| `__debugbreak` | ✗ | Trap opcode — pending |
| `__int8` / `__int16` / `__int32` / `__int64` | ✗ | Spelling aliases of `<stdint.h>` types — pending |
| `__try` / `__except` / `__finally` / `__leave` | ✗ | SEH; parsing is non-trivial and tied to the Windows kernel unwinder — see [#289](https://todo.sr.ht/~takeiteasy/jcc/289) |
| `__declspec(uuid)` / `__uuidof` | ✗ | COM-specific; not applicable to C — see [#289](https://todo.sr.ht/~takeiteasy/jcc/289) |
| `__clrcall` / `__interface` / `__if_exists` / `#pragma managed` | ✗ | C++/CLI-specific; rejected — see [#289](https://todo.sr.ht/~takeiteasy/jcc/289) |
| `__readfsbyte` / `__readgsbyte` / `__readcr*` | ✗ | Segment-prefix intrinsics — pending |
| `_ReturnAddress` / `_AddressOfReturnAddress` | ✗ | VM frame inspection opcode — pending |
| `_InterlockedCompareExchange*` / `_InterlockedExchange*` / `_InterlockedIncrement*` / `_InterlockedAdd*` | ✗ | Layered on existing `__atomic_*` opcodes — pending |
| `_rotl8/16/32/64` / `_rotr8/16/32/64` | ✗ | Rotate intrinsics — pending |
| `_bittest*` / `_bittestandset` / `_bittestandreset` / `_bittestandcomplement` | ✗ | Bit-test intrinsics — pending |
| `__popcnt16/32/64` | ✗ | Population count intrinsics — pending |
| `__cpuid` / `__cpuidex` | ✗ | Host CPU info — pending |
| `__emul` / `__emulu` / `_umul128` / `__umulh` | ✗ | 64-bit multiplication helpers — pending |
| `#pragma once` | ✓ | |
| `#pragma comment(lib, "x")` | ✗ | Link hint, no-op — pending |
| `#pragma warning(push/pop/disable/default)` / `suppress:` | ✗ | Maps to JCC's `-W` system — pending |
| `#pragma pack(...)` | ✓ | |
| `#pragma message("...")` | ✓ | |
| `#pragma region` / `#pragma endregion` | ✗ | IDE-only, no-op — pending |
| `#pragma intrinsic(...)` / `#pragma function(...)` | ✗ | Intrinsic toggle, no-op — pending |
| `#pragma optimize(...)` | ✗ | No-op — pending |
| `#pragma loop(...)` | ✗ | Loop-hint pragma, no-op — pending |
| `#pragma data_seg` / `bss_seg` / `code_seg` | ✗ | Section placement, no-op — pending |
| `#pragma section(...)` | ✗ | No-op — pending |
| `#pragma init_seg("lib")` | ✗ | No-op — pending |
| `#pragma runtime_checks(...)` / `strict_gs_check` | ✗ | No-op — pending |
| `__pragma(...)` | ✗ | In-macro pragma wrapper — pending |
| `_MSC_VER` / `_MSC_FULL_VER` / `_MSC_BUILD` | ✗ | JCC-specific value in compat header — pending |
| `_MSVC_LANG` / `_MSC_EXTENSIONS` | ✗ | `201710L` / `1` in compat header — pending |
| `_MSC_WARNING_DURATION` | ✗ | `0` in compat header — pending |
| `__FUNCSIG__` / `__FUNCDNAME__` / `__FUNCTION__` | ✗ | Implementable via reflection — pending |
| `__COUNTER__` | ✗ | Pre-existing extension? verify and document — pending |

---

## Built-in Functions

JCC supports a subset of GCC's `__builtin_*` functions. These are parsed and
lowered directly in the compiler — they do not require any header include and
do not link against host libc. A catch-all ticket
([#215](https://todo.sr.ht/~takeiteasy/jcc/215)) tracks remaining GNU builtins
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

## Attributes

JCC supports GNU `__attribute__((...))` and C23 `[[...]]` attribute syntaxes.
The most common diagnostic and layout attributes are fully implemented; the
rest are **parsed and silently ignored** by the attribute consumer.

### Quick Reference

| Attribute | Syntax | Status | Semantics |
|-----------|--------|--------|-----------|
| `aligned(N)` | GNU | ✓ | Sets minimum alignment on types and variables |
| `packed` | GNU | ✓ | Suppresses struct member padding |
| `unused` / `__unused__` | GNU | ✓ | Suppresses `-Wunused` warnings |
| `deprecated` / `__deprecated__` | GNU | ✓ | Emits `-Wdeprecated` warnings |
| `deprecated("msg")` | GNU | ✓ | Emits `-Wdeprecated` with custom message |
| `maybe_unused` | C23 | ✓ | Suppresses `-Wunused` warnings |
| `deprecated` | C23 | ✓ | Emits `-Wdeprecated` warnings |
| `deprecated("msg")` | C23 | ✓ | Emits `-Wdeprecated` with custom message |
| `macro` | GNU | ✓ | JCC-specific; compile-time macro (see [MACROS.md](MACROS.md)) |
| `comptime` | GNU | ✓ | JCC-specific; compile-time variable evaluation (see [MACROS.md](MACROS.md)) |
| *all others* | Both | ~ | Parsed and silently ignored — see [Parsed but Ignored](#parsed-but-ignored) |

### Supported Attributes

#### `__attribute__((aligned(N)))`

Sets minimum alignment for a type or variable. The argument is a constant expression specifying the alignment in bytes. Can also be used without an argument (`__attribute__((aligned))`) to request maximum useful alignment.

**Source:** `src/parse.c:3945-3954`

```c
struct __attribute__((aligned(16))) vec4 { float x, y, z, w; };
int __attribute__((aligned(64))) cache_line;
```

#### `__attribute__((packed))`

Prevents the compiler from inserting padding between struct/union members, and can also prevent alignment-based padding at the end of a struct.

**Source:** `src/parse.c:3938-3942`, `src/parse.c:4140-4163`

```c
struct __attribute__((packed)) {
    char c;
    int i;  // directly follows c with no padding
};
```

#### `__attribute__((unused))` / `__attribute__((__unused__))` / `[[maybe_unused]]`

Suppresses `-Wunused` warnings on variables, functions, parameters, typedefs, and labels. Both the GNU and C23 forms (`[[maybe_unused]]`) are recognised with full semantic effect.

**Source:** `src/parse.c:3956-3976` (GNU), `src/parse.c:4031` (C23)

```c
int __attribute__((unused)) x;       // GNU
int [[maybe_unused]] y;              // C23
__attribute__((unused)) static void helper(void) {}
```

#### `__attribute__((deprecated))` / `__attribute__((__deprecated__))` / `[[deprecated]]`

Marks a declaration as deprecated. Warnings are emitted via `-Wdeprecated` when the identifier is used. Supports an optional message string that is included in the warning output.

**Source:** `src/parse.c:3957-3976` (GNU), `src/parse.c:4032-4040` (C23), `src/parse.c:199-206` (warning emission), `src/parse.c:4977-4990` (use-site checks)

```c
int __attribute__((deprecated("use bar instead"))) old_func(void);
int [[deprecated]] legacy_var;
```

#### `__attribute__((comptime))` / `__attribute__((comptime))` (JCC-specific)

These are JCC's own extensions for compile-time metaprogramming. They are intercepted by the preprocessor and do not reach the general attribute parser. See [MACROS.md](MACROS.md) for details.

**Source:** `src/preprocess.c:1737-1796`

```c
[[jcc::comptime]] int square(int x) { return x * x; }
__attribute__((comptime)) const int version = 42;
```

#### `#include_comptime` (JCC-specific)

Includes a header only during the comptime compilation pass. The header and
any macros or types it defines are invisible to the runtime translation unit.
Use this when a `[[jcc::comptime]]` helper needs a dependency (e.g.
`<glob.h>`, `<dirent.h>`) that must not bleed into runtime code.

**Source:** `src/preprocess.c` (`PP_INCLUDE_COMPTIME` case), `src/macros.c` (`build_combined_macro_tokens`)

```c
#include_comptime <glob.h>

[[jcc::comptime]]
int glob_struct_size(void) { return (int)sizeof(glob_t); }
```

See [MACROS.md — Comptime-only includes](MACROS.md) for full documentation.

#### `__jcc_forward_include` (JCC-specific)

Reflection API function callable from macro bodies. Registers a header to be
prepended as an `#include` directive in the serialized C output. Duplicate
registrations for the same header are deduplicated.

**Source:** `src/reflect.c` (`__jcc_forward_include`), `src/serialize.c` (`cc_serialize_program`)

```c
[[jcc::comptime]]
void gen_helpers(void) {
    $forward_include("<string.h>");
    // ... generate functions that call strlen() ...
}
```

See [MACROS.md — Forward includes in generated output](MACROS.md) for full documentation.

### Parsed but Ignored

Any GNU `__attribute__` identifier that is not explicitly handled (i.e., not `packed`, `aligned`, `unused`/`__unused__`, or `deprecated`/`__deprecated__`) is **consumed and silently ignored**. The parser skips the attribute name and any parenthesised argument list, then continues.

**Source:** `src/parse.c:3979-3997`

Similarly, any C23 `[[...]]` attribute other than `maybe_unused` or `deprecated` is **consumed and silently ignored** by the C23 attribute parser.

**Source:** `src/parse.c:4010-4058`

Ignored attributes include (but are not limited to):

| Attribute | Syntax | Tracking |
|-----------|--------|----------|
| `nodiscard` | C23 | [#219](https://todo.sr.ht/~takeiteasy/jcc/219) |
| `fallthrough` | C23 | Statement-level attribute support pending; [#219](https://todo.sr.ht/~takeiteasy/jcc/219) |
| `no_unique_address` | C23 | [#219](https://todo.sr.ht/~takeiteasy/jcc/219) |
| `noreturn` | C23/GNU | [#216](https://todo.sr.ht/~takeiteasy/jcc/216) |
| `pure` | GNU | [#217](https://todo.sr.ht/~takeiteasy/jcc/217) |
| `const` | GNU | [#217](https://todo.sr.ht/~takeiteasy/jcc/217) |
| `cleanup` | GNU | [#218](https://todo.sr.ht/~takeiteasy/jcc/218) |
| `format(printf,...)` | GNU | [#214](https://todo.sr.ht/~takeiteasy/jcc/214) |
| `visibility` | GNU | |
| `section` | GNU | |
| `weak` | GNU | |
| `weakref` | GNU | |
| `alias` | GNU | |
| `constructor` / `destructor` | GNU | |
| `hot` / `cold` | GNU | |
| `always_inline` / `flatten` / `noinline` | GNU | |
| `returns_nonnull` / `nonnull` | GNU | |
| `malloc` | GNU | |
| `alloc_size` / `alloc_align` | GNU | |
| `sentinel` | GNU | |
| `format_arg` | GNU | |
| `warn_unused_result` | GNU | |
| `unsequenced` | C23 | |
| `reproducible` | C23 | |

### Open Tickets

| # | Attribute | Priority | Description |
|---|-----------|----------|-------------|
| [#214](https://todo.sr.ht/~takeiteasy/jcc/214) | `format(printf, fmt, args)` | medium-high | Type-check printf/scanf format strings at compile time |
| [#215](https://todo.sr.ht/~takeiteasy/jcc/215) | Catch-all | medium | Remaining GNU builtins and attributes |
| [#216](https://todo.sr.ht/~takeiteasy/jcc/216) | `noreturn` / `[[noreturn]]` | high | Mark functions that never return; integrate with control-flow analysis |
| [#217](https://todo.sr.ht/~takeiteasy/jcc/217) | `pure` / `const` | medium | Side-effect-free function annotations for optimisation |
| [#218](https://todo.sr.ht/~takeiteasy/jcc/218) | `cleanup(func)` | medium | Scope-based cleanup callbacks (RAII-style) |
| [#219](https://todo.sr.ht/~takeiteasy/jcc/219) | `nodiscard`, `fallthrough`, `no_unique_address` | medium | Remaining standard C23 attributes |

The JCC-specific `@`-prefix attribute syntax is tracked separately in
[#234](https://todo.sr.ht/~takeiteasy/jcc/234).

### Position in Grammar

Attributes are accepted at these positions in the grammar:

| Position | GNU `__attribute__` | C23 `[[...]]` | Source Location |
|----------|---------------------|---------------|-----------------|
| Storage class specifier sequence | ✓ | ✓ | `src/parse.c:770-775` |
| Before declarator (prefix) | ✓ | ✓ | `src/parse.c:1110-1112` |
| After declarator (suffix) | ✓ | ✓ | `src/parse.c:1168-1170` |
| Before abstract declarator | ✓ | ✓ | `src/parse.c:1183-1185` |
| Struct/union — before tag | ✓ | ✓ | `src/parse.c:4063-4064` |
| Struct/union — after body | ✓ | ✓ | `src/parse.c:4094-4095` |
| Enum specifier | ✓ | ✓ | `src/parse.c:1280-1281` |
| Labels | ✓ | ✓ | `src/parse.c:2569-2578` |
| Statement level | ✗ | ✗ | Not yet supported |

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
| `<sys/wait.h>` | ✓ | Process wait (`wait`, `waitpid`), `WNOHANG`, `WUNTRACED`, `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`, `WTERMSIG`, `WIFSTOPPED`, `WSTOPSIG`, `WCOREDUMP` |
| `<termios.h>` | ✓ | Terminal I/O (`tcgetattr`, `tcsetattr`, `struct termios`, `cc_t`, `speed_t`, `tcflag_t`) |
| `<unistd.h>` | ✓ | Core POSIX API (`read`, `write`, `close`, `lseek`, `access`, `unlink`, `rmdir`, `chdir`, `getcwd`, `getpid`, `getppid`, `sleep`, `usleep`, `pipe`, `fork`, `execv`, `execve`, `execl`, `execlp`, `execle`, `execvp`, `_exit`, `ssize_t`, `STDIN/STDOUT/STDERR_FILENO`, `SEEK_*`, `F_*`/`R_*`/`W_*`/`X_OK`) |
| `<utime.h>` | ✓ | File time manipulation (`utime`, `struct utimbuf`) |

---

## Not Supported

| Feature | Notes |
|---|---|
| Threading (`<threads.h>`, `pthread`) | JCC is single-threaded |
| Atomic operations (`<stdatomic.h>` operations) | Headers present; operations are non-atomic |
| Complex function call ABI | Passing or returning complex values by function call is not implemented |
| Full native ABI for runtime `dlsym` calls | Runtime dynamic function calls support scalar/pointer signatures through libffi using the current scalar/double metadata. Aggregate by-value arguments/returns, callbacks, variadic function-pointer calls, and full platform ABI descriptors are not implemented |
| Native code generation | JCC produces VM bytecode only |
| Shared-library auto-linking for arbitrary undeclared symbols | `dlfcn.h` calls are available; `--library` opens requested libraries for registered FFI symbols |

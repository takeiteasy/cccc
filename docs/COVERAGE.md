# C Language Coverage

Conformance status for each C standard, plus GNU and Microsoft extension
syntaxes that CCCC accepts. Also covers attribute syntax and standard library
support. Intended as a reference for `--std` flag work and as a checklist of
what is currently parsed vs. what is semantically honoured.

| Status | Meaning |
|---|---|
| ✓ | Fully supported |
| ~ | Partial — accepted by the compiler but behaviour is incomplete or approximated |
| ✗ | Not supported |

---

## Runtime Threading

POSIX `<pthread.h>` is partially supported on POSIX hosts through VM-managed
pthread handles backed by host pthreads. VM bytecode execution is serialized by
a recursive global interpreter lock, while blocking pthread calls such as
`pthread_join`, `pthread_mutex_lock`, and `pthread_cond_wait` release the GIL.
This provides pthread correctness and blocking/wakeup semantics, not parallel
bytecode execution.

C11 `<threads.h>` and language thread-local storage are fully implemented.
Real atomic operations via `<stdatomic.h>` macros use atomic-tagged opcodes and
runtime mixed-access detection, tracked separately from the POSIX pthread layer.

---

## C89 / C90

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
| `volatile` | ✓ | Volatile locals routed via generic LDR/STR (watchpoint-safe, C11 §6.7.3p7) |
| `register` | ~ | Parsed and accepted; ignored |
| `auto` (storage class) | ~ | Parsed and accepted; ignored — deprecated in C23 (use `auto` type inference instead) |
| String literals and concatenation | ✓ | |
| `L"..."` wide string literals | ✓ | Parsed; stored as UTF-32 |
| `L'...'` wide character literals | ✓ | |
| K&R-style function definitions | ✓ | |
| Trigraphs | ✗ | Removed in C23; intentionally not supported |
| Digraphs (`<:` `:>` `<%` `%>` `%:` `%:%:`) | ✓ | Equivalent to `[` `]` `{` `}` `#` `##` (C23 §6.4.6); original spelling preserved during stringification |
| `#include`, `#define`, `#undef` | ✓ | |
| `#ifdef`, `#ifndef`, `#if`, `#elif`, `#else`, `#endif` | ✓ | |
| CCCC-routed preprocessor directives | ✓ | `@emit` routes directives to generated output; `@comptime` routes directives to the comptime stream |
| `#error` | ✓ | |
| `#line` | ~ | Directive parsed; line tracking updated |
| Object-like and function-like macros | ✓ | |
| Macro stringification `#` and token-pasting `##` | ✓ | |
| Forward declarations and incomplete types | ✓ | |
| Multiple translation units / linker | ✓ | |

---

## C99

Supported C99, C11, and C23 language features may also be accepted as
extensions in older `--std` modes. Enable `-Wpedantic` to diagnose those
pre-standard uses, or `-Werror=pedantic` to reject them.

| Feature | Status | Notes |
|---|---|---|
| `//` single-line comments | ✓ | |
| `long long int` and `unsigned long long int` | ✓ | |
| `_Bool` | ✓ | |
| `_Complex` | ✓ | Native scalar representation with arithmetic, casts, assignment, and equality |
| `_Imaginary` | ~ | Accepted as compatibility spelling for the corresponding complex type. Tickets [#278](https://todo.sr.ht/~takeiteasy/cccc/278) / [#279](https://todo.sr.ht/~takeiteasy/cccc/279) closed WONT_FIX |
| Mixed declarations and statements | ✓ | |
| Variable declaration in `for` initialiser | ✓ | |
| Variable-length arrays (VLA) | ✓ | Allocated via VM heap |
| Flexible array members (`struct { int n; int arr[]; }`) | ✓ | |
| Designated initialisers — structs and arrays | ✓ | |
| Compound literals | ✓ | |
| `inline` functions | ✓ | Dead-function elimination + single-return inlining (unconditional); full AST inlining at `-O2`/`-O3` (`--inline-limit=N` controls size threshold) |
| `restrict` pointers | ✓ | Parsed and stored on `Type`; codegen exploits non-aliasing: straight-line loads through `*restrict_param` or `restrict_param[const]` are cached in callee-saved registers (cache key is `(param, byte_offset)`, so `p[0]`, `p[1]`, etc. occupy separate slots); stores through a constant index update only that slot, while variable-index stores invalidate all slots for that param; `for (i=0;i<n;i++) dst[i]=src[i]` loops with both pointers restrict-qualified are lowered to a single `MCPY` opcode ([#267](https://todo.sr.ht/~takeiteasy/cccc/267), [#268](https://todo.sr.ht/~takeiteasy/cccc/268)); locals provably derived from restrict params (e.g. `int *q = p + 1`) inherit the non-aliasing property via a single-function AST pre-pass, extending the deref cache to `*q` and `q[const]` patterns ([#269](https://todo.sr.ht/~takeiteasy/cccc/269)) |
| Type qualifiers in array-parameter indices (`void f(int a[const static 10])`) | ✓ | `static` enforces minimum-size, emitting `-Wstatic-array-size` when a constant-size argument is too small (best-effort: bare pointer args not checked). `const`/`volatile`/`restrict` inside `[...]` are applied to the decayed pointer (e.g. `[const N]` → `int *const`); VLA-form qualifiers (`[const n]`) not yet adjusted. |
| `__func__` predefined identifier | ✓ | |
| Variadic macros `__VA_ARGS__` | ✓ | |
| `_Pragma(...)` operator | ✓ | |
| Hexadecimal floating-point literals (`0x1.8p+1`) | ✓ | |
| `u8"..."` UTF-8 string literals | ✓ | |
| `u"..."` UTF-16 string literals | ✓ | |
| `U"..."` UTF-32 string literals | ✓ | |
| `u'...'` and `U'...'` character literals | ✓ | |
| Universal character names `\uXXXX` / `\UXXXXXXXX` | ✓ | |
| Trailing comma in enumerator list | ✓ | |
| Integer constant expressions — stricter rules | ✓ | |

---

## C11

| Feature | Status | Notes |
|---|---|---|
| `_Generic` type-generic expressions | ✓ | |
| `_Alignof` | ✓ | |
| `_Alignas` | ✓ | |
| `_Static_assert` (and `static_assert` via `<assert.h>`) | ✓ | |
| `_Noreturn` | ✓ | Accepted via keyword, `__attribute__((noreturn))`, and `[[noreturn]]`; emits BTRAP after calls; warns on returns |
| `_Thread_local` | ✓ | TLS segment; each thread receives a private copy from the template |
| `_Atomic` types | ~ | Parser emits `-Wignored-features`; direct access to `_Atomic`-qualified variables uses plain load/store. `<stdatomic.h>` macros (`atomic_load/store/exchange/compare_exchange`, `atomic_fetch_*`) emit ALDR/ASTR/AXCHG/ACAS opcodes with runtime shadow-tracking and mixed-access detection. Cross-thread correctness requires the GIL |
| Anonymous structs and unions | ✓ | |
| `char16_t` / `char32_t` types | ✓ | Provided by `<uchar.h>` |
| `u8`, `u`, `U` string and character literal prefixes | ✓ | See C99 row; support predates formal C11 adoption |

---

## C17 / C18

C17 is a bug-fix release — no new language features were added. All C11
language coverage figures apply.

---

## C23

| Feature | Status | Notes |
|---|---|---|
| `typeof` / `typeof_unqual` | ✓ | |
| `constexpr` for objects | ✓ | Object definitions require constant initializers and may be used in constant-expression contexts; constexpr functions are not supported |
| `thread_local` storage-class spelling | ✓ | C23 keyword; allocates in the TLS segment |
| Compound literal storage classes | ✓ | C23 `(static T){...}`, `(constexpr T){...}`, `(register T){...}`, and TLS spellings are parsed; static/constexpr/TLS literals use anonymous static storage, while register keeps automatic storage |
| `auto` type inference | ✓ | Deduces type as `typeof_unqual(initializer)` with array-to-pointer and function-to-pointer decay; pointer declarators (`auto *p = &x`) validated; initializer required |
| `nullptr` keyword / `nullptr_t` | ✓ | `nullptr_t` is defined in `<stddef.h>` via `typeof(nullptr)` |
| `_BitInt(N)` arbitrary-precision integers | ✓ | `N` in `[1,65535]` (`BITINT_MAXWIDTH`). `N<=64` uses scalar-register storage with mask/shift truncation; `N>64` uses multi-word (address-based) storage with runtime helper functions for arithmetic, shifts, comparisons, and conversions. `wb`/`uwb` literal suffixes infer their full-precision width directly from the literal's digit text, including widths beyond 64 bits |
| Binary integer literals `0b10101010` | ✓ | |
| Digit separators `1'000'000` | ✓ | |
| `[[...]]` attributes | ~ | Parsed; see [Attributes](#attributes) below for per-attribute status |
| `bool`, `true`, `false` as keywords (not just macros) | ✓ | Real keywords in `--std=c23`/`gnu23`; downgraded to ordinary identifiers below C23. `<stdbool.h>` still works (its macros are gated to pre-C23) |
| `u8` character literals (`u8'x'`) | ✓ | |
| Unnamed function parameters (`void f(int, double)`) | ✓ | |
| `static_assert` without message | ✓ | C23 one-argument form |
| Improved `enum` — underlying type, forward declaration, wide values | ✓ | `enum E : unsigned char { … }` sets size/align/signedness; `enum E : int;` forward-declares; values stored as `int64_t` (C23 §6.7.2.2) |
| Compatible tag redeclarations | ✓ | C23 same-scope compatible `struct`, `union`, and `enum` redeclarations are accepted; incompatible redeclarations are diagnosed |
| Decimal floating-point (`_Decimal32`, etc.) | ~ | `_Decimal32/64/128` accepted with correct sizes (4/8/16 bytes) but implemented as aliases of `float`/`double`/`long double` (binary, not decimal, encoding); real IEEE-754-2008 decimal arithmetic tracked in a follow-up ticket |
| `char8_t` | ✓ | Defined in `<uchar.h>`; `u8'x'` literals have type `unsigned char` and value `char8_t`; `mbrtoc8`/`c8rtomb` implement the full §7.31.1 incremental UTF-8 state machine |
| Labels before declarations (at block scope) | ✓ | `case`, `default`, and goto-labels may directly precede object declarations; pre-C23 bare declaration after label is a hard error |
| Empty parameter lists `()` — C23 prototype semantics | ✓ | Pre-C23: `()` is an unprototyped (K&R) declaration accepting any arguments; in C23, `()` is equivalent to `(void)`. Use `-Wstrict-prototypes` to warn about non-prototype `()` in pre-C23 modes |
| `exp10`, `sinpi`/`cospi`/`tanpi`, `asinpi`/`acospi`/`atanpi`/`atan2pi` (`<math.h>`) | ~ | `double`/`long double` variants implemented (native on macOS/glibc where available, portable shims otherwise, with exact integer/half-integer special-casing for `sinpi`/`cospi`/`tanpi`); `f` variants registered but affected by a pre-existing float-FFI limitation ([#406](https://todo.sr.ht/~takeiteasy/cccc/406)) |

### Preprocessor

| Feature | Status | Notes |
|---|---|---|
| `#elifdef` / `#elifndef` | ✓ | |
| `#warning` | ✓ | |
| `#embed` | ✓ | Supports `limit()`, `prefix()`, `suffix()`, `if_empty()`, `__has_embed()` |
| `__VA_OPT__` | ✓ | |
| `__has_c_attribute` | ✓ | Returns C23 version date (`202311L`) for standard C23 attributes; `1` for CCCC vendor attributes |
| `__has_include` | ✓ | Checks CCCC, `-I`, and `-i` include paths |

---

## GNU Extensions

| Feature | Status | Notes |
|---|---|---|
| Statement expressions `({ ... })` | ✓ | |
| 128-bit integers `__int128` / `__int128_t` / `__uint128_t` | ✓ | Implemented on top of the `_BitInt(128)` machinery (multi-word, address-based storage). `unsigned __int128` is honoured; `__SIZEOF_INT128__` is defined so feature-detecting code selects these paths |
| `__attribute__((...))` | ~ | Parsed; `aligned`, `packed`, `unused`, `deprecated`, `format`, `nodiscard`, `warn_unused_result`, `fallthrough`, `noreturn` supported (see [Attributes](#attributes) below) |
| Labels as values `&&label` | ✓ | |
| Computed goto `goto *expr` | ✓ | |
| Switch case ranges `case 1 ... 5:` | ✓ | |
| Zero-length arrays `int arr[0]` | ✓ | |
| Empty structs and unions | ✓ | GNU extension; empty aggregates have size 0 |
| Nested functions | ✓ | Access to parent-scope variables via static link |
| Blocks `^{ ... }` (Clang/Apple) | ✓ | Capture-by-value plus `__block` by-reference; nest to arbitrary depth (transitive capture through enclosing descriptors); `Block_copy` heap-duplicates the descriptor so a block can escape its frame, `Block_release` frees that copy |
| `__builtin_*` | ✓ | Lowered by the compiler; see [Built-in Functions](#built-in-functions) below |
| `__thread` storage class | ✓ | TLS segment; per-thread private storage |
| `__restrict` / `__restrict__` | ✓ | Spelling aliases for `restrict`; fully optimised (see `restrict` entry above) |
| `__inline` / `__inline__` | ✓ | Spelling aliases for `inline`; recognized as GCC keyword aliases (GCC compatibility) |
| `__typeof__` | ✓ | Synonym for `typeof` |
| `asm(...)` inline assembly | ✓ | `asm(...)` statements are no-ops by default; `--asm-passthru` compiles via native CC and executes via FFI; custom callback via `cc_set_asm_callback`; `__asm__` statement spelling is pending |
| GNU `asm("symbol")` declaration labels | ~ | Supported on function declarations, including typedef-based and multi-declarator declarations; the label is used as the external FFI symbol name |

---

## Microsoft Extensions

A subset of MSVC's compiler extensions is recognised by CCCC. The feasibility,
no-op policy, and the question of which extensions to support versus reject
are open design questions tracked in
[#289](https://todo.sr.ht/~takeiteasy/cccc/289). The table below mirrors the
groups from that ticket.

| Feature | Status | Notes |
|---|---|---|
| `__declspec(align(n))` | ✗ | Spelling alias of `__attribute__((aligned(n)))` — pending |
| `__declspec(deprecated)` | ✗ | Maps to existing `-Wdeprecated` — pending |
| `__declspec(dllimport)` / `dllexport` | ✗ | CCCC module export — pending |
| `__declspec(naked)` | ✗ | No-op in the VM — pending |
| `__declspec(noalias)` / `noinline` / `inline` | ✗ | Hint-only — pending |
| `__declspec(noreturn)` / `nothrow` | ✗ | C11 equivalents exist — pending |
| `__declspec(restrict)` | ✗ | Not yet implemented (the #267–#269 restrict work covers the `restrict` qualifier, not this MSVC spelling) — future ticket |
| `__declspec(safebuffers)` / `selectany` / `code_seg` / `allocate` | ✗ | No-op shims — pending |
| `__declspec(thread)` | ✗ | TLS via VM per-thread storage — pending |
| `__cdecl` / `__stdcall` / `__fastcall` / `__thiscall` / `__vectorcall` | ✗ | Calling-convention keywords, no-op (CCCC has a single VM ABI) — pending |
| `__ptr32` / `__ptr64` / `__sptr` / `__uptr` / `__unaligned` / `__w64` | ✗ | Pointer modifiers, no-op — pending |
| `__forceinline` | ~ | Folds to `inline` — pending `__declspec` alias |
| `__assume(expr)` | ✗ | Optimizer hint — pending |
| `__noop` | ✗ | Variable-arg no-op builtin — pending |
| `__debugbreak` | ✗ | Trap opcode — pending |
| `__int8` / `__int16` / `__int32` / `__int64` | ✗ | Spelling aliases of `<stdint.h>` types — pending |
| `__try` / `__except` / `__finally` / `__leave` | ✗ | SEH; parsing is non-trivial and tied to the Windows kernel unwinder — see [#289](https://todo.sr.ht/~takeiteasy/cccc/289) |
| `__declspec(uuid)` / `__uuidof` | ✗ | COM-specific; not applicable to C — see [#289](https://todo.sr.ht/~takeiteasy/cccc/289) |
| `__clrcall` / `__interface` / `__if_exists` / `#pragma managed` | ✗ | C++/CLI-specific; rejected — see [#289](https://todo.sr.ht/~takeiteasy/cccc/289) |
| `__readfsbyte` / `__readgsbyte` / `__readcr*` | ✗ | Segment-prefix intrinsics — pending |
| `_ReturnAddress` / `_AddressOfReturnAddress` | ✗ | VM frame inspection opcode — pending |
| `_InterlockedCompareExchange*` / `_InterlockedExchange*` / `_InterlockedIncrement*` / `_InterlockedAdd*` | ✗ | Layered on existing `__atomic_*` opcodes — pending |
| `_rotl8/16/32/64` / `_rotr8/16/32/64` | ✗ | Rotate intrinsics — pending |
| `_bittest*` / `_bittestandset` / `_bittestandreset` / `_bittestandcomplement` | ✗ | Bit-test intrinsics — pending |
| `__popcnt16/32/64` | ✗ | Population count intrinsics — pending |
| `__cpuid` / `__cpuidex` | ✗ | Host CPU info — pending |
| `__emul` / `__emulu` / `_umul128` / `__umulh` | ✗ | 64-bit multiplication helpers — pending |
| `#pragma once` | ✓ | |
| `#pragma comment(lib, "x")` | ✓ | Alternate form of `#pragma cccc link("x")` — queues a library for FFI resolution (and native linking) |
| `#pragma warning(push/pop/disable/default)` / `suppress:` | ✗ | Maps to CCCC's `-W` system — pending |
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
| `_MSC_VER` / `_MSC_FULL_VER` / `_MSC_BUILD` | ✗ | CCCC-specific value in compat header — pending |
| `_MSVC_LANG` / `_MSC_EXTENSIONS` | ✗ | `201710L` / `1` in compat header — pending |
| `_MSC_WARNING_DURATION` | ✗ | `0` in compat header — pending |
| `__FUNCSIG__` / `__FUNCDNAME__` / `__FUNCTION__` | ✗ | Implementable via reflection — pending |
| `__COUNTER__` | ✗ | Pre-existing extension? verify and document — pending |

---

## Attributes

CCCC supports GNU `__attribute__((...))` and C23 `[[...]]` attribute syntaxes.
The most common diagnostic and layout attributes are fully implemented; the
rest are **parsed and silently ignored** by the attribute consumer.

### Feature-Test Preprocessor Operators

CCCC provides the common `__has_*` operators in preprocessor conditionals:
`__has_include`, `__has_feature`, `__has_extension`, `__has_attribute`,
`__has_builtin`, `__has_c_attribute`, and `__has_cpp_attribute`.

`__has_feature` and `__has_extension` report selected-standard support for
`c99`, `c11`, `c23`, `c_alignas`, `c_alignof`, `c_generic_selections`, and
`c_static_assert`. Parsed-but-incomplete features such as `_Atomic` and
`_Thread_local` intentionally return `0`.

`__has_attribute` and `__has_builtin` return `1` only for attributes and builtins
with compiler semantics; parsed-but-ignored attributes return `0`.
`__has_c_attribute` returns the C23 version date (`202311L`) for standard C23
attributes (C23 N3220 §6.10.10.2) and `1` for CCCC vendor attributes;
unsupported or unknown attributes return `0`. `__has_cpp_attribute` returns `0`.

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
| `macro` | GNU | ✓ | CCCC-specific; compile-time macro (see [MACROS.md](MACROS.md)) |
| `comptime` | GNU | ✓ | CCCC-specific; compile-time variable evaluation (see [MACROS.md](MACROS.md)) |
| `format(printf/scanf, …)` | GNU | ✓ | Type-check printf/scanf format strings at compile time, including length-modifier-aware argument type validation (`%ld` → `long`, `%zu` → `unsigned long`, `%Lf` → `long double`); gated by `-F`; also accepts GNU/Clang alternate spellings `__printf__`, `gnu_printf`, `printf0`, `__printf0__`, `__scanf__`, `gnu_scanf`, `strftime`, `__strftime__`, `os_log`, `__os_log__` (latter two parsed without validation) |
| `nodiscard` | C23 | ✓ | Warns on discarded return values (`-Wnodiscard`, part of `-Wall`) |
| `fallthrough` | C23 | ✓ | Suppresses fallthrough warning in switch cases (`-Wfallthrough`, part of `-Wextra`) |
| `noreturn` | C23 / GNU | ✓ | Emits `BTRAP` after calls; warns on returns |
| `optimize("ON")` / `optimize(N)` | GNU / CCCC | ✓ | Per-function optimization level (0–4); attribute wins over global `-O` |
| `cleanup(fn)` | GNU | ✓ | Scope-exit callback: calls `fn(&var)` when the variable goes out of scope |
| *all others* | Both | ~ | Parsed and silently ignored — see [Parsed but Ignored](#parsed-but-ignored) |

### Supported Attributes

#### `__attribute__((aligned(N)))`

Sets minimum alignment for a type or variable. The argument is a constant expression specifying the alignment in bytes. Can also be used without an argument (`__attribute__((aligned))`) to request maximum useful alignment.

```c
struct __attribute__((aligned(16))) vec4 { float x, y, z, w; };
int __attribute__((aligned(64))) cache_line;
```

#### `__attribute__((packed))`

Prevents the compiler from inserting padding between struct/union members, and can also prevent alignment-based padding at the end of a struct.

```c
struct __attribute__((packed)) {
    char c;
    int i;  // directly follows c with no padding
};
```

#### `__attribute__((unused))` / `__attribute__((__unused__))` / `[[maybe_unused]]`

Suppresses `-Wunused` warnings on variables, functions, parameters, typedefs, and labels. Both the GNU and C23 forms (`[[maybe_unused]]`) are recognised with full semantic effect.

```c
int __attribute__((unused)) x;       // GNU
int [[maybe_unused]] y;              // C23
__attribute__((unused)) static void helper(void) {}
```

#### `__attribute__((deprecated))` / `__attribute__((__deprecated__))` / `[[deprecated]]`

Marks a declaration as deprecated. Warnings are emitted via `-Wdeprecated` when the identifier is used. Supports an optional message string that is included in the warning output.

```c
int __attribute__((deprecated("use bar instead"))) old_func(void);
int [[deprecated]] legacy_var;
```

#### `__attribute__((comptime))` / `[[cccc::comptime]]` (CCCC-specific)

These are CCCC's own extensions for compile-time metaprogramming. They are intercepted by the preprocessor and do not reach the general attribute parser. See [MACROS.md](MACROS.md) for details.

```c
[[cccc::comptime]] int square(int x) { return x * x; }
__attribute__((comptime)) const int version = 42;
__comptime int helper(void) { return 42; }
```

#### `#include [[cccc::comptime]]` / `#include @comptime` (CCCC-specific)

Includes a header only during the comptime compilation pass. The header and
any macros or types it defines are invisible to the runtime translation unit.
Use this when a `[[cccc::comptime]]` helper needs a dependency (e.g.
`<glob.h>`, `<dirent.h>`) that must not bleed into runtime code.

```c
#include [[cccc::comptime]] <glob.h>
#include @comptime <glob.h>
#include __attribute__((comptime)) <glob.h>

[[cccc::comptime]]
int glob_struct_size(void) { return (int)sizeof(glob_t); }
```

See [MACROS.md — Comptime-only includes](MACROS.md) for full documentation.

#### Preprocessor `[[cccc::emit]]` / `@emit` / `__attribute__((emit))` (CCCC-specific)

Routes preprocessor directives to serialized generated output. Place the marker
immediately after the directive keyword:

```c
#include [[cccc::emit]] <string.h>
#include @emit <stdint.h>
#include __attribute__((emit)) <stddef.h>
#ifdef @emit _WIN32
#define CCCC_PLATFORM_WINDOWS 1
#endif @emit
```

Emit includes are deduplicated. Other emitted directives keep source order, so
they can wrap declarations generated by file-scope macro calls. Use an emit
block for several raw preprocessor directives. Emit blocks require an enclosing
comptime context and act as a runtime escape hatch — ordinary C declarations
inside are compiled into the runtime translation unit and copied to generated
output:

```c
#pragma cccc comptime begin
#pragma cccc emit begin
#ifdef _WIN32
#define CCCC_PLATFORM_WINDOWS 1
#endif
int win_helper(void);    // compiled into runtime TU and emitted
#pragma cccc emit end
#pragma cccc comptime end
```

See [MACROS.md — Emit directives and includes in generated output](MACROS.md) for full documentation.

#### Preprocessor `[[cccc::comptime]]` / `@comptime` / `__attribute__((comptime))` (CCCC-specific)

Routes a preprocessor directive to the comptime compilation stream instead of
runtime source:

```c
#define @comptime CT_VALUE 42
#ifdef @comptime CT_VALUE
#define @comptime CT_SEEN 1
#endif @comptime
```

### Side-Effect Annotations

#### `__attribute__((pure))` / `[[gnu::pure]]`

Marks a function as *pure*: it may read global state but has no side effects
and always returns the same result for the same arguments given unchanged
global state.

**Optimizer effect:**

- **`--optimize=1` and above**: Dead-call elimination — calls whose return
  value is discarded are omitted entirely.  Argument expressions are still
  evaluated so their side effects run.

Pure functions are **not** eligible for common-subexpression elimination
because their result may change if a global is modified between two calls.

```c
__attribute__((pure)) int strlen_pure(const char *s) { /* ... */ }

// Dead call at -O1+: result discarded, but side effects of ++n still run.
strlen_pure(buf);  // result unused — call omitted
```

Accepted spellings: `__attribute__((pure))`, `__attribute__((__pure__))`,
`[[gnu::pure]]`, `[[cccc::pure]]`.

---

#### `__attribute__((const))` / `[[gnu::const]]`

Marks a function as *const*: it has no side effects and its return value
depends only on its arguments (no global reads).  Stronger than `pure`.

**Optimizer effect:**

- **`--optimize=1` and above**: Dead-call elimination (same as `pure`).
- **`--optimize=2` and above**: Common-subexpression elimination (CSE) — if
  the same const function is called more than once with the same argument
  values within a straight-line block, the second call is replaced by a
  register move reusing the first result.  CSE fires when all argument
  registers hold known value numbers (compile-time constants or unmodified
  local-variable loads).

```c
[[gnu::const]] int square(int x) { return x * x; }

int a = square(5);  // called normally
int b = square(5);  // same constant arg — second call eliminated at -O2+
```

Accepted spellings: `__attribute__((const))`, `__attribute__((__const__))`,
`[[gnu::const]]`, `[[cccc::const]]`.

> **Note:** `gnu::const` would collide with the C keyword `const` if written
> as `[[const]]` — the `gnu::` namespace qualifier is required for the C23
> spelling.

---

#### `__attribute__((optimize(...)))` / `[[cccc::optimize(N)]]` / `@optimize(N)` (CCCC-specific)

Controls the optimization level for a single function, independently of the
global `-O` flag.  CCCC uses **GCC-style attribute precedence**: the attribute
always overrides the global level for that function, regardless of what
`-O`, `--optimize=N`, or `#pragma cccc config(optimisation=N)` specify.

This is the inverse of CCCC's `#pragma cccc config` rule (where CLI wins) —
for the `optimize` attribute the function's explicit annotation always takes
priority.

| Level | Behaviour |
|-------|-----------|
| 0 | No optimization for this function (useful for timing-stable or debug code) |
| 1 | Constant folding + dead-call elimination |
| 2 | Level 1 + peephole + CSE for const functions |
| 3 | Level 2 + dead-code elimination |
| 4 | Level 3 + opcode fusion |

**Three accepted spellings:**

```c
// 1. GCC-compatible GNU attribute — string form "ON" or "-ON"
__attribute__((optimize("O2")))
int hot_fn(int a, int b) { return a + b; }

// 2. C23 cccc-native integer
[[cccc::optimize(3)]]
static int aggressive_fn(int x) { return x * x; }

// 3. @ shorthand (rewrites to [[cccc::optimize(...)]])
@optimize(2)
static long search(const long *arr, long n) { /* ... */ }
```

The string form (`"O2"` / `"-O2"`) accepts `O0` through `O4` (with an
optional leading `-`), matching GCC's `__attribute__((optimize("O2")))`.
The integer form accepts `0` through `4` directly.

**Interaction with global optimization:**

```c
// With -O0 globally:
//   - aggressive_fn is still optimized at level 3 (attribute wins)
//   - plain_fn runs at level 0 (no attribute → follows global)
[[cccc::optimize(3)]]
int aggressive_fn(int x) { return x * x; }

int plain_fn(int x) { return x * 2; }  // follows -O flag
```

**`__has_attribute` / `__has_c_attribute`:** both return `1` for `optimize`
(as a CCCC vendor attribute).

---

#### `__attribute__((cleanup(fn)))` / `[[gnu::cleanup(fn)]]`

Registers a scope-exit callback `fn` for a local variable. When the variable
goes out of scope, `fn(&var)` is called automatically. `fn` must have the
signature `void fn(T *)` where `T` is the type of the variable.

```c
void cleanup_free(int **p) { free(*p); }

void example(void) {
    int *buf __attribute__((cleanup(cleanup_free))) = malloc(100 * sizeof(int));
    // buf is automatically freed when example() returns or the block exits
}
```

**Call order:** LIFO — the last-declared variable is cleaned up first within a
scope.

**Scope exit paths covered:**

| Exit kind | Cleanup fires? |
|-----------|---------------|
| Natural block end (`}`) | ✓ |
| `return` | ✓ |
| `break` | ✓ |
| `continue` | ✓ |
| Named `goto` out of scope | ✓ |
| `longjmp` | ✗ (matches GCC C-mode behavior) |

**Return value preservation:** when a non-void return is combined with cleanup
calls, the return value is preserved across cleanup invocations. For integer or
pointer returns, the value is saved via a stack push; for float/double returns,
a dedicated stack slot is used.

**Static inline cleanup functions** referenced only through the attribute are
kept alive (not dead-stripped) by the liveness pass.

**Goto and scope ancestry:** every named `goto` cleans up exactly the cleanup
scopes between it and the lowest common ancestor it shares with the target label.
This covers cross-sibling jumps (from one block into a sibling at the same
nesting depth) — the source block's variables are still cleaned. Jumping *into*
the scope of a cleanup variable (past its declaration) is ill-formed C and is
diagnosed under `-Wattributes`, since the variable would be uninitialized when
its cleanup runs.

**Limitations:**

- `longjmp` does not trigger cleanup, matching GCC C-mode behavior.

**`__has_attribute`:** returns `1` for `cleanup`.

---

### Parsed but Ignored

Any GNU `__attribute__` identifier that is not explicitly handled (i.e., not `packed`, `aligned`, `unused`/`__unused__`, `deprecated`/`__deprecated__`, or `warn_unused_result`/`__warn_unused_result__`) is **consumed and emits a `-Wattributes` warning**. The parser skips the attribute name and any parenthesised argument list, then continues.

Similarly, any C23 `[[...]]` attribute other than `maybe_unused`, `deprecated`, `nodiscard`, `fallthrough`, or `noreturn` is **consumed and emits a `-Wattributes` warning**.

Ignored attributes include (but are not limited to):

| Attribute | Syntax | Tracking |
|-----------|--------|----------|
| `no_unique_address` | C23 | Parsed but ignored — VM optimisation deferred |
| `visibility` | GNU | |
| `section` | GNU | |
| `weak` | GNU | |
| `weakref` | GNU | |
| `alias` | GNU | |
| `constructor` / `destructor` | GNU | |
| `hot` / `cold` | GNU | |
| `always_inline` / `flatten` / `noinline` | ~ | `always_inline`/`flatten` fold to inline hint; `noinline` parsed — semantics pending `__attribute__` integration with inline pass |
| `returns_nonnull` / `nonnull` | GNU | |
| `malloc` | GNU | |
| `alloc_size` / `alloc_align` | GNU | |
| `sentinel` | GNU | |
| `format_arg` | GNU | |
| `unsequenced` | C23 | |
| `reproducible` | C23 | |

### Open Tickets

| # | Attribute | Priority | Description |
|---|-----------|----------|-------------|
| [#215](https://todo.sr.ht/~takeiteasy/cccc/215) | Catch-all | medium | Remaining GNU builtins and attributes |

### `@`-prefix Attribute Syntax

CCCC supports a concise `@name` / `@name(args)` shorthand that rewrites to
the canonical attribute form before parsing:

| Usage | Rewrites to | Example |
|-------|-------------|---------|
| `@name` (CCCC-specific) | `[[cccc::name]]` | `@comptime`, `@test`, `@test_setup` |
| `@name` (standard C23) | `[[name]]` | `@nodiscard`, `@maybe_unused` |
| `@name` (GNU / unknown) | `__attribute__((name))` | `@packed`, `@aligned(16)` |
| `@name` (custom comptime) | handler registered by `@comptime(attribute("name"))` | `@serialize struct Point { ... };` |

Resolution order: CCCC-specific attributes are checked first (they become
`[[cccc::name(...)]]`), then standard C23 attributes (they become
`[[name(...)]]`), then registered custom comptime attributes, then GNU
attributes (they become `__attribute__((name(...)))`). Unrecognised names fall
back to the GNU form.

CCCC-specific attributes also accept double-underscore keyword aliases:
`__comptime`, `__comptime__`, `__macro`, `__macro__`, `__test`, `__test__`,
`__test_setup`, `__test_setup__`, `__test_teardown`, and
`__test_teardown__`. Plain names such as `comptime`, `macro`, and `test`
remain ordinary identifiers.

Generated and preprocessed output never includes CCCC-specific syntax. `-E`,
`-M`, `-G`, and `-c=native` strip CCCC-only attributes and route markers before
emitting C for another compiler. Use `--attr-target=auto|c23|gnu|msvc|strip`
to select how remaining attributes are printed. `auto` emits standard C23
attributes as `[[...]]` in C23 mode and uses GNU `__attribute__((...))`
otherwise; GNU-only attributes such as `packed` stay GNU in `auto` mode.

```c
@comptime
Node *make_answer(void) { return MakeIntLiteral(42); }

@test(suite = "math")
void test_add(void) { AssertEq(1 + 1, 2); }

struct @packed point { char x; int y; };
```

The `@` prefix is accepted wherever the corresponding canonical form is valid.
Custom comptime attributes currently run only on file-scope declarations
(types, typedefs, functions, and globals). Their arguments are parsed as
expression AST nodes and passed to the registered handler after the decorated
target has been built.
`__declspec` (Windows) is reserved as a future fourth fallback category.

### Position in Grammar

Attributes are accepted at these positions in the grammar:

| Position | GNU `__attribute__` | C23 `[[...]]` |
|----------|---------------------|---------------|
| Storage class specifier sequence | ✓ | ✓ |
| Before declarator (prefix) | ✓ | ✓ |
| After declarator (suffix) | ✓ | ✓ |
| Before abstract declarator | ✓ | ✓ |
| Struct/union — before tag | ✓ | ✓ |
| Struct/union — after body | ✓ | ✓ |
| Enum specifier | ✓ | ✓ |
| Labels | ✓ | ✓ |
| Statement level | ✗ | ✗ |

### GNU `asm("symbol")` Labels

GNU asm labels are accepted after function declarators. CCCC keeps the C
identifier for source lookup and uses the label as the external FFI symbol:

```c
typedef int Print(const char *);

Print say asm("puts");

int main(void) {
    Print a asm("puts"), b asm("puts");
    say("file scope");
    a("first");
    b("second");
}
```

Object asm labels are parsed as declaration syntax but do not change storage
layout or serialized output.

---

## Standard Library and Built-in Functions

CCCC provides a built-in standard library (embedded via `src/std.c`) and
a subset of GCC's `__builtin_*` functions. The standard library headers
in `include/` are compiled into the binary; the builtins are lowered directly
in the compiler and do not require any header include or host libc linkage.

### Built-in Functions

CCCC supports a subset of GCC's `__builtin_*` functions. These are parsed and
lowered directly in the compiler — they do not require any header include and
do not link against host libc. A catch-all ticket
([#215](https://todo.sr.ht/~takeiteasy/cccc/215)) tracks remaining GNU builtins
not yet implemented.

#### Math Constants

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

#### Math Predicates

These are lowered to equivalent arithmetic comparisons at parse time.

| Builtin | Description |
|---------|-------------|
| `__builtin_isnan(x)` | Non-zero if `x` is NaN |
| `__builtin_isinf(x)` | Non-zero if `x` is infinite |
| `__builtin_isfinite(x)` | Non-zero if `x` is finite (not NaN, not infinite) |
| `__builtin_signbit(x)` | Non-zero if `x` is negative |

#### Compiler Introspection

| Builtin | Description |
|---------|-------------|
| `__builtin_constant_p(expr)` | `1` if `expr` is a compile-time constant, else `0` |
| `__builtin_types_compatible_p(t1, t2)` | `1` if types `t1` and `t2` are compatible |
| `__builtin_choose_expr(c, e1, e2)` | Compile-time select: `e1` if constant `c` is non-zero, else `e2`. Result carries the chosen arm's type; the unchosen arm is parsed but not evaluated |
| `__builtin_reg_class(type)` | `0` = integer/pointer, `1` = float, `2` = other |
| `__builtin_expect(expr, hint)` | Returns `expr`; `hint` is a branch-prediction hint (ignored) |
| `__builtin_offsetof(type, member)` | Compile-time offset of `member` within `type` |

#### Memory and Control Flow

| Builtin | Description |
|---------|-------------|
| `__builtin_alloca(size)` | Dynamically allocate `size` bytes on the stack |
| `__builtin_alloca_with_align(size, align)` | Like `__builtin_alloca`; `align` is in bits and must be a constant. Only 16-byte alignment is guaranteed; finer alignment is accepted but not enforced. |
| `__builtin_frame_address(0)` | Returns the current frame's base pointer (level 0 only) |
| `__builtin_unreachable()` | Marks an unreachable code path; halts the VM if executed |

#### String Builtins

These are forwarded to libc via the FFI and require the matching libc function to be available. They do not require `#include <string.h>` but are compatible with it (the `extern` declaration in `<string.h>` merges cleanly with the builtin registration).

| Builtin | Return type | Description |
|---------|-------------|-------------|
| `__builtin_strlen(s)` | `long` | Equivalent to `strlen(s)` |
| `__builtin_strcmp(a, b)` | `int` | Equivalent to `strcmp(a, b)` |

#### Variadic Argument Macros (`__builtin_va_*`)

When `<stdarg.h>` is included, the following macros are available as aliases for the standard `va_*` macros:

| Macro | Equivalent |
|-------|------------|
| `__builtin_va_start(ap, last)` | `va_start(ap, last)` |
| `__builtin_va_end(ap)` | `va_end(ap)` |
| `__builtin_va_copy(d, s)` | `va_copy(d, s)` |
| `__builtin_va_arg(ap, type)` | `va_arg(ap, type)` |

These require `ap` to be of type `va_list` (the struct defined in `<stdarg.h>`). `__builtin_va_list` is defined as `char*` for macOS system-header compatibility and is a separate type.

#### Atomic Operations

| Builtin | Description |
|---------|-------------|
| `__builtin_compare_and_swap(addr, old, new)` | CAS; returns bool |
| `__builtin_atomic_exchange(addr, val)` | Atomic exchange; returns old value |

#### Bit-Manipulation Builtins

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

#### Checked Arithmetic Builtins

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

### Standard Library Headers

#### C89 / C90

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
| `<time.h>` | ✓ | Time/date (`time`, `mktime`, `timegm`, `localtime`/`localtime_r`, `gmtime`/`gmtime_r`, `strftime`, `difftime`, `clock`, `nanosleep`), `struct tm`, `struct timespec` |

#### C99

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

#### C11

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

#### C17 / C18

C17 is a bug-fix release — no new language features or library functions were added. All C11 coverage figures apply.

| Change | Status | Notes |
|---|---|---|
| Removes `gets` | ✓ | `gets` is not registered in CCCC's stdlib |
| Deprecates `ATOMIC_VAR_INIT` | N/A | Atomics not supported |
| Clarifies undefined behaviour | N/A | Semantic, not syntactic |

#### C23

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

### POSIX Headers

POSIX headers are embedded and backed by host OS calls. They are only available on POSIX targets (not Windows).

| Header | Status | Notes |
|---|---|---|
| `<arpa/inet.h>` | ✓ | Network byte-order conversion (`htonl`, `htons`, `ntohl`, `ntohs`), address manipulation (`inet_addr`, `inet_ntoa`, `inet_ntop`, `inet_pton`) |
| `<dirent.h>` | ✓ | Directory entry iteration (`opendir`, `readdir`, `closedir`, `DIR`, `struct dirent`) |
| `<dlfcn.h>` | ✓ | VM-managed dynamic loading (`dlopen`, `dlsym`, `dlclose`, `dlerror`); `dlsym` function symbols are callable through typed function pointers for scalar/pointer signatures |
| `<fcntl.h>` | ✓ | File control (`open`, `creat`, `fcntl`), `O_*` and `S_*` permission constants, record-locking `F_*` commands, `FD_CLOEXEC`, `struct flock` |
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
| `<sys/file.h>` | ✓ | Advisory file locking (`flock`, `LOCK_SH`, `LOCK_EX`, `LOCK_NB`, `LOCK_UN`) |
| `<sys/ioctl.h>` | ✓ | Device control (`ioctl`, `struct winsize`, `TIOCGWINSZ`, `TIOCSWINSZ`) |
| `<sys/mman.h>` | ✓ | Memory management (`mmap`, `munmap`, `mprotect`, `msync`, `posix_madvise`), `PROT_*`, `MAP_*`, `MAP_FAILED`, `MS_*`, `MADV_*` constants |
| `<sys/mount.h>` | ✓ | Filesystem statistics (`statfs`, `fstatfs`, `struct statfs`) — minimal portable field set |
| `<sys/param.h>` | ✓ | System limits and helpers (`MAXPATHLEN`, `NBBY`, `MIN`, `MAX`) |
| `<sys/socket.h>` | ✓ | Socket API (`socket`, `bind`, `listen`, `accept`, `connect`, `setsockopt`, `getsockname`, `shutdown`, `struct sockaddr`, `socklen_t`) |
| `<sys/stat.h>` | ✓ | File status (`stat`, `fstat`, `lstat`, `chmod`, `fchmod`, `mkdir`, `mkfifo`, `umask`), `struct stat`, `S_*` constants and macros |
| `<sys/time.h>` | ✓ | Time operations (`gettimeofday`, `settimeofday`, `utimes`), `struct timeval`, `struct timezone`, `timeradd`, `timersub`) |
| `<sys/types.h>` | ✓ | Basic system types (`dev_t`, `ino_t`, `mode_t`, `nlink_t`, `uid_t`, `gid_t`, `off_t`, `pid_t`, `blksize_t`, `blkcnt_t`, `useconds_t`, `sa_family_t`, `socklen_t`) |
| `<sys/wait.h>` | ✓ | Process wait (`wait`, `waitpid`), `WNOHANG`, `WUNTRACED`, `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`, `WIFSTOPPED`, `WSTOPSIG`, `WCOREDUMP` |
| `<termios.h>` | ✓ | Terminal I/O (`tcgetattr`, `tcsetattr`, `struct termios`, `cc_t`, `speed_t`, `tcflag_t`) |
| `<unistd.h>` | ✓ | Core POSIX API (`read`, `write`, `pread`, `pwrite`, `close`, `lseek`, `access`, `unlink`, `rmdir`, `chdir`, `getcwd`, `getpid`, `getppid`, `getuid`, `geteuid`, `getgid`, `getegid`, `fchown`, `chown`, `readlink`, `symlink`, `fdatasync`, `getpagesize`, `sleep`, `usleep`, `pipe`, `fork`, `execv`, `execve`, `execl`, `execlp`, `execle`, `execvp`, `_exit`, `ssize_t`, `STDIN/STDOUT/STDERR_FILENO`, `SEEK_*`, `F_OK`/`R_OK`/`W_OK`/`X_OK`, `_SC_PAGESIZE`) |
| `<utime.h>` | ✓ | File time manipulation (`utime`, `struct utimbuf`) |

---

### Shim Implementations

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

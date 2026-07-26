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
| `exp10`, `sinpi`/`cospi`/`tanpi`, `asinpi`/`acospi`/`atanpi`/`atan2pi` (`<math.h>`) | ✓ | `double`/`float`/`long double` variants implemented (native on macOS/glibc where available, portable shims otherwise, with exact integer/half-integer special-casing for `sinpi`/`cospi`/`tanpi`) |

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
| `__attribute__((...))` | ~ | Parsed; `aligned`, `packed`, `unused`, `deprecated`, `format`, `nodiscard`, `warn_unused_result`, `fallthrough`, `noreturn`, `error`, `warning`, `constructor`, `destructor`, `sentinel`, `alloc_size`, `malloc` supported (see [Attributes](#attributes) below) |
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
| `__attribute__((vector_size(N)))` generic vectors | ~ | 128-, 256-, and 512-bit vectors (16/32/64-byte total size) — e.g. `v4f32`/`v8f32`/`v16f32`, `v2f64`/`v4f64`/`v8f64`, `v4i32`/`v8i32`/`v16i32`, and the corresponding `i64`/`i16`/`i8` lane layouts at each width; any other width (non-power-of-two byte counts, or wider than 64 bytes) is rejected with a diagnostic. Element-wise `+ - * /` and unary `-`/`~` on all lane types; integer lanes additionally support `% & \| ^` and integer `/`/`%`, each trapping per-lane on a zero divisor or `MIN/-1` overflow (stricter than default scalar `/`, which does not trap — matches scalar `DIVC`'s policy); `&`/`\|`/`^`/`~` are rejected on float lanes. Comparisons `== != < <= > >=` produce a per-lane all-ones(`-1`)/all-zero mask in a same-width **signed** integer vector (GCC semantics); ordered `< <=` on unsigned-int lanes compare the unsigned view. GNU vector `?:` select is supported (nonzero-per-lane condition, matching lane count/width). Scalar operands broadcast through arithmetic (`v + 5.0f`), matching GCC/clang — a bare scalar cannot initialize or be assigned to a whole vector. `v[i]` subscript supports a runtime-variable index. Brace-initializer syntax (`v4sf a = {1,2,3,4};`, including partial and nested forms) is supported, as are compound literals (`(v4sf){1,2,3,4}`) used as expressions, with a `static` storage-class specifier, or as the entire initializer of another global/static variable (`v4sf g = (v4sf){1,2,3,4};`). Designated lane initializers (`{[2]=3.0f}`) are rejected with a diagnostic, matching GCC/clang — vector types are non-aggregate in their model, and designated initializers only apply to aggregates. `__builtin_convertvector(expr, type)` converts between vectors with the same lane count; only `int32<->float32` and `int64<->float64` lane pairs are representable (matching lane counts forces matching element byte sizes), so same-domain conversions (e.g. changing signedness/width without crossing int/float) are rejected. `__builtin_shuffle` supports both a **compile-time-constant, brace-enclosed** index mask (`__builtin_shuffle(v, {3,2,1,0})`, 1- or 2-vector form — CCCC's own constant-mask form, closer to clang's `__builtin_shufflevector` than upstream GCC's syntax) and a **runtime or named integer vector mask** (`__builtin_shuffle(v, mask)` / `__builtin_shuffle(v1, v2, mask)`, matching upstream GCC's general form) — the mask must be an integer vector with the same lane count and element byte width as the vector being shuffled. Both forms lower via the same vector-subscript machinery with no dedicated opcode; the constant form range-checks each index at compile time (an out-of-range index is a compile error), while the runtime form takes each index modulo the lane count (1-vector) or twice the lane count (2-vector), matching GCC's documented wraparound semantics — a runtime index can't be rejected at compile time. Vectors can be passed as function parameters and returned by value: passed by memory (a caller-side scratch copy sized to the argument's own width, address handed to the callee, like a struct-by-value arg), returned via the RETBUF rotating pool (like a struct-by-value return) — see [VM.md](VM.md#simd--vector-operations). This includes a variadic `...` parameter: the by-memory scratch-pointer convention means a variadic vector arg always occupies exactly one 8-byte slot regardless of the vector's width, and `<stdarg.h>`'s `va_arg` detects a vector `type` via `__builtin_classify_type` and dereferences the slot instead of reading it directly — matches gcc/clang, which also accept a vector through `...` (verified). Not supported through the native FFI marshalling path (extern/`dlopen`ed functions — libffi has no portable vector type, and there's no struct-by-value FFI path to build on either) or a GNU/Apple block invocation — each rejected with a diagnostic. See [VM.md](VM.md#simd--vector-operations) for the opcode set |

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

`__has_builtin` returns `1` only for builtins with compiler semantics.
`__has_attribute` returns `1` for attributes with compiler semantics, and also
for a set of GNU attributes that are architecturally inert in a bytecode-VM
target (no ELF/Mach-O output, no linker, no per-function ISA codegen, no
inliner, no branch-temperature layout, no symbol-level DCE, no strict-aliasing
optimizer, no machine-mode type system — see
[Parsed but Ignored](#parsed-but-ignored)) but are recognized and consumed by
name, matching real GCC/Clang so headers that feature-test on them don't
diverge; genuinely unrecognized attributes return `0`.
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
| `error("msg")` | GNU | ✓ | Emits a compile-time error when called; DCE-aware: suppressed inside statically-dead positions (constant-fold + unsigned boundary tautology): `if`/`else` branches, `while(0)`/`for(;0;)` bodies and increment expressions, `false && call()` / `true \|\| call()` short-circuit operands, ternary `cond ? dead : live` branches, GNU elvis `truthy ?: dead` — enabling the `_FORTIFY_SOURCE` `__chk_fail` idiom |
| `warning("msg")` | GNU | ✓ | Emits a compile-time warning when called; same DCE-aware suppression as `error` |
| `warn_unused_result` / `__warn_unused_result__` | GNU | ✓ | GNU equivalent of `[[nodiscard]]`: warns if the return value is discarded (`-Wnodiscard`, part of `-Wall`) |
| `nonnull` / `nonnull(N,...)` | GNU / C23 | ✓ | Warns when a statically- or flow-provably-null argument is passed to a nonnull-marked parameter (`-Wnonnull`, part of `-Wall`); a merely maybe-null argument warns under the opt-in `-Wmaybe-nonnull` |
| `returns_nonnull` | GNU / C23 | ✓ | Warns when a statically- or flow-provably-null value is returned from a `returns_nonnull` function (`-Wnonnull`, part of `-Wall`); a merely maybe-null return warns under the opt-in `-Wmaybe-nonnull` |
| `constructor` / `constructor(N)` | GNU (C23: `[[gnu::constructor]]`) | ✓ | Runs `void(void)` function before `main()`, ordered by priority (lower first; unprioritised functions run last) |
| `destructor` / `destructor(N)` | GNU (C23: `[[gnu::destructor]]`) | ✓ | Runs `void(void)` function after `main()` returns normally, in reverse priority order (higher first; unprioritised functions run first) |
| `sentinel` / `sentinel(N)` | GNU (C23: `[[gnu::sentinel]]`) | ✓ | Warns at each call site when the expected trailing variadic argument is not a literal, pointer-typed `NULL` (`-Wsentinel`, part of `-Wall`); static syntactic check only, no runtime enforcement. Applying it to a non-variadic function warns under `-Wattributes` instead |
| `alloc_size(n)` / `alloc_size(n,m)` | GNU (C23: `[[gnu::alloc_size]]`) | ✓ | Marks a function as an allocator whose return value has a compile-time-computable byte size: argument `n` (1-based) for the single-index form, or the product of arguments `n` and `m` for the two-index (calloc-style) form. Consulted by `__builtin_object_size`/`__builtin_dynamic_object_size` heap-allocation sizing (see below) — the sole recognition mechanism, superseding earlier hardcoded name matching |
| `malloc` | GNU (C23: `[[gnu::malloc]]`) | ~ | Parsed and stored (self-describes a fresh, non-aliasing allocator, matching libc's `malloc`/`calloc`/`aligned_alloc`) but not yet wired to any aliasing optimization or nonnull inference — informational only; the aliasing optimizations GCC uses it for need a memory-dependency pass the VM optimizer doesn't have yet (see `__attribute__((malloc))` below) |
| *all others* | Both | ~ | Parsed and silently ignored — see [Parsed but Ignored](#parsed-but-ignored) |

`__has_attribute` returns `1` for `error`, `warning`, `warn_unused_result`, and
`__warn_unused_result__` — all four carry real compile-time semantics (see
above), matching real GCC/Clang.

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

**`__has_attribute`:** returns `1` for `pure` and `__pure__`.

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

**`__has_attribute`:** returns `1` for `const` and `__const__`.

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

#### `__attribute__((constructor))` / `__attribute__((destructor))` / `[[gnu::constructor]]` / `[[gnu::destructor]]`

Registers a `void(void)` function to run before `main()` (constructor) or
after `main()` returns (destructor). An optional integer priority controls
relative ordering among multiple constructors/destructors:

```c
__attribute__((constructor)) void init(void) { /* runs before main() */ }
__attribute__((constructor(101))) void init_early(void) { /* lower number runs first */ }

__attribute__((destructor)) void fini(void) { /* runs after main() returns */ }
[[gnu::destructor(101)]] void fini_late(void) { /* higher number runs first */ }
```

**Ordering:** constructors run in ascending priority order (lower numbers
first); functions with no explicit priority form the default group and run
last. Destructors run in the reverse order — descending priority (higher
numbers first), with the default group running first. A simple stable sort
is used; CCCC does not replicate full ELF `.init_array` priority-band
semantics.

**Limitations:**

- Destructors run only on a **normal return from `main()`**. A guest
  `exit()`, `_Exit()`, or `abort()` call passes straight through to the host
  libc function and terminates the process without returning to CCCC's
  startup path, so registered destructors do **not** run on those paths.
- Constructors and destructors must have signature `void(void)` — the GCC
  extension allowing constructors to receive `argc`/`argv`/`envp` is not
  supported.
- A constructor's writes to `_Thread_local` variables are not visible to
  `main()` or later destructors: each of these runs as an independent
  `cc_run_at` cycle with its own freshly-copied TLS segment. Non-TLS globals
  are unaffected — they live in the shared data segment.
- Constructors/destructors only run around `main()` (`cc_run`). `-t`/
  `--testing` mode (`[[cccc::test]]` discovery) invokes test functions
  directly and does not go through `cc_run`, so they do not run in that mode.

**`__has_attribute`:** returns `1` for `constructor` and `destructor`.

---

#### `__attribute__((nonnull))` / `__attribute__((nonnull(N,...)))` / `[[gnu::nonnull]]` and `__attribute__((returns_nonnull))` / `[[gnu::returns_nonnull]]`

Marks pointer parameters (bare `nonnull`, or `nonnull(1,3)` for specific
1-based argument indices) as never null, and `returns_nonnull` marks a
function's return value as never null. Both are static, compile-time-only
checks — there is no codegen effect.

At each call site, an argument in a nonnull position (or a `return` in a
`returns_nonnull` function) is checked in two ways:

- A **compile-time constant** that folds to zero, e.g. a literal
  `NULL`/`0`/`(void*)0`, is always caught.
- A light **flow-sensitive pass** additionally tracks simple local pointer
  variables through straight-line code within a single function body, so a
  null value that reaches the call through a variable is also caught:

```c
void handle(int *p) __attribute__((nonnull(1)));
void handle(int *p) { *p = 1; }

int *make(void) __attribute__((returns_nonnull));
int *make(void) { return 0; }   // warns: null returned from function declared with 'returns_nonnull'

int main(void) {
    handle(0);                  // warns: null passed to a parameter marked nonnull (parameter 1)

    int *p = 0;
    handle(p);                  // warns: null value passed to a parameter marked nonnull (parameter 1)

    make();
    return 42;
}
```

Under plain `-Wnonnull`, the flow pass only warns on **provably-null**
values — a pointer whose address has been taken is never tracked.

A pointer that is only **maybe** null — definitely null on one path through
a branch but not on all of them, e.g.:

```c
int *p = 0;
if (cond) p = &x;
handle(p);   // -Wnonnull: silent; -Wmaybe-nonnull: warns
```

is diagnosed separately under the opt-in `-Wmaybe-nonnull`, via real
per-branch merge dataflow (each live branch of an `if`/ternary/`&&`/`||` is
walked independently and the resulting null-states are joined at the merge
point). `-Wmaybe-nonnull` has a higher false-positive rate on real code than
plain `-Wnonnull`, so it is never implied by `-Wall` or `-Wextra` — pass it
explicitly. Loops and `switch` are not merged precisely by either flag (a
local assigned anywhere inside is conservatively treated as unknown on exit,
so no maybe-null warning fires there).

A limited form of **interprocedural** tracking also feeds the same
`-Wmaybe-nonnull` lattice: a whole-translation-unit pass flags any
pointer-returning function that has a provable null-returning path (a
literal `return 0;`/`return NULL;` on some reachable branch), regardless of
where in the file it's defined relative to its callers. A direct call to a
flagged function, assigned to a local, is treated as maybe-null:

```c
int *maybe_null(int cond) { return cond ? &x : 0; }

void use(void) {
    int *p = maybe_null(1);
    handle(p);   // -Wmaybe-nonnull: warns; maybe_null() may return null
}
```

This is deliberately conservative in the safe direction: only a function
with a **visible body** in the current translation unit and a **provable**
null-returning path is flagged — an unannotated external or
declaration-only function (e.g. `malloc`) is never assumed to maybe-return
null, since that would warn on nearly every unannotated pointer-returning
call. It propagates through a direct call to a flagged function whether that
call is first assigned to a local or used inline as the argument/return
expression itself:

```c
handle(maybe_null(1));               // -Wmaybe-nonnull: warns, same as above

int *wrap(void) __attribute__((returns_nonnull));
int *wrap(void) { return maybe_null(1); }   // -Wmaybe-nonnull: warns
```

A transitive call chain — a function whose *only* null-returning path is
itself a call to another flagged function (`return other_maybe_null_fn();`,
as opposed to a literal `return 0;`) — is also covered: the summary pass
iterates to a fixpoint, so chains of any depth converge regardless of source
order:

```c
int *relay(int cond) { return maybe_null(cond); }  // relay flagged too, transitively

void use(void) {
    int *p = relay(1);
    handle(p);   // -Wmaybe-nonnull: warns
}
```

When a flagged function is provably null on **every** reachable path (never
just some), the whole-TU pass promotes it from a maybe-null fact to a
definite one — mirroring the same NN_NULL-vs-NN_MAYBE distinction the
intra-function flow analysis already makes for local variables:

```c
int *always_null(void) { return 0; }

void use(void) {
    int *p = always_null();
    handle(p);   // -Wnonnull: warns (needs -Wmaybe-nonnull passed too, to
                 // run the interprocedural pass that discovers the fact)
}
```

Note this still requires `-Wmaybe-nonnull` to be passed for the
interprocedural pass to run at all — the always-null fact is diagnosed
under `-Wnonnull`'s message text once discovered, but discovering it in the
first place stays gated behind the opt-in flag, same as every other
interprocedural fact here. Passing `-Wmaybe-nonnull` alone (without
`-Wnonnull`) does **not** warn on an always-null callee, again mirroring the
local-variable convention: `-Wmaybe-nonnull` covers *maybe*-null evidence,
and a provably-always-null value is `-Wnonnull`'s concern.

Diagnosed under `-Wnonnull` (part of `-Wall`) and `-Wmaybe-nonnull`
(opt-in only); disable with `-Wno-nonnull` / `-Wno-maybe-nonnull`.

**`__has_attribute`:** returns `1` for `nonnull`, `__nonnull__`,
`returns_nonnull`, and `__returns_nonnull__`.

#### `__attribute__((sentinel))` / `__attribute__((sentinel(N)))` / `[[gnu::sentinel]]`

Marks a variadic function as requiring a `NULL`-terminated argument list —
the classic example is `execl()`/`execlp()`. Bare `sentinel` requires the
*last* argument to be a literal `NULL`; `sentinel(N)` allows `N` trailing
non-sentinel arguments to follow the required `NULL` (i.e. the `NULL` must
be `N` positions back from the end of the call).

This is a **static, call-site-only** check — a syntactic scan of the parsed
argument list, with no runtime enforcement. Only a literal/constant-folded
*pointer-typed* null (`NULL`, `(void*)0`, `(T*)0`, C23 `nullptr`) satisfies
the check; a variable that merely holds `NULL` at runtime still warns, since
the parser has no flow analysis over the value (unlike `-Wmaybe-nonnull`'s
dataflow pass — sentinel checking mirrors GCC's own purely-syntactic
behaviour here). A bare, uncast `0` (`int`-typed) also warns, distinctly from
a fully-missing terminator, since it is not guaranteed to zero-fill a
pointer-sized `va_list` slot — matching GCC's `-Wsentinel` strictness.

`sentinel` only makes sense on a variadic function; applying it to a
non-variadic function warns `"sentinel attribute only applies to variadic
functions"` under `-Wattributes` at the declaration, and the (necessarily
argument-less) call sites are not additionally flagged.

```c
void run(const char *path, ...) __attribute__((sentinel));
void run(const char *path, ...) { }

int main(void) {
    run("/bin/ls", "-l", (void*)0);   // ok
    run("/bin/ls", "-l", 0);          // warns: bare 0 is not a pointer; cast NULL / (void*)0
    run("/bin/ls", "-l");             // warns: missing sentinel in function call
}
```

```c
// sentinel(1): one trailing argument is allowed after the NULL
void run2(const char *path, ...) __attribute__((sentinel(1)));
void run2(const char *path, ...) { }

int main(void) {
    run2("/bin/ls", "-l", (void*)0, extra_flag);   // ok
}
```

If the call does not supply enough variadic arguments for the expected
sentinel position to exist at all, CCCC warns `"not enough variable
arguments to fit a sentinel"` instead of indexing past the end of the
argument list.

Diagnosed under `-Wsentinel` (part of `-Wall`); disable with `-Wno-sentinel`.

#### `__attribute__((alloc_size(n)))` / `__attribute__((alloc_size(n,m)))` / `[[gnu::alloc_size]]`

Marks a function as returning a pointer to an allocation whose byte size is
given by argument `n` (1-based), or by the product of arguments `n` and `m`
for the two-index form (e.g. `calloc`-style `nmemb * size`). This is the
sole mechanism `__builtin_object_size`/`__builtin_dynamic_object_size` use to
recognize heap allocators — it supersedes an earlier hardcoded name match
against `malloc`/`calloc`/`realloc`/`aligned_alloc`, so any function
(including a project's own arena/pool allocator) participates in heap-size
tracking once annotated, and a function that merely shares an allocator's
*name* but carries no attribute is correctly left untracked:

```c
void *arena_alloc(size_t n) __attribute__((alloc_size(1)));
void *arena_alloc(size_t n) { return malloc(n); }

int main(void) {
    char *p = arena_alloc(64);
    __builtin_object_size(p, 0); // 64 — tracked via the attribute, not the name

    void *my_malloc(int n); // a same-named lookalike with no attribute
    void *q = my_malloc(64);
    __builtin_object_size(q, 0); // (size_t)-1 — conservative, untracked
}
```

libc's `malloc`, `calloc`, `realloc`, `aligned_alloc`, and `reallocarray` are
declared with `alloc_size` in `include/stdlib.h`, so they self-describe the
same way. See the `__builtin_object_size`/`__builtin_dynamic_object_size`
entries in the Builtins table below for the soundness rules (reassignment/
address-of poisoning) that gate when the tracked size is actually trusted.

#### `__attribute__((malloc))` / `[[gnu::malloc]]`

Marks a function as returning a fresh, non-aliasing pointer (matching GCC's
`malloc` attribute). Parsed and stored on the function type, but currently
informational only — CCCC does not yet exploit it for aliasing optimizations,
and it deliberately does **not** imply `nonnull`/`returns_nonnull` (a real
allocator can return `NULL` on failure). libc's `malloc`, `calloc`, and
`aligned_alloc` carry it in `include/stdlib.h`; `realloc`/`reallocarray` do
not, since they may return the same block as their input pointer.

Exploiting the attribute (as GCC/Clang do, to justify reordering loads/stores
around a call and eliding dead stores) needs a memory-dependency / alias
analysis the bytecode optimizer (`src/optimize.c`) doesn't have: there is no
dead-store elimination, no load/store reordering, and no general "does pointer
A alias pointer B" reasoning anywhere in the pass pipeline — stores are
deliberately never removed by the DCE pass, and the CSE pass's only memory
model is an exact-offset local-slot cache that a call invalidates outright
regardless of `malloc`. The closest existing aliasing consumer, the
`restrict`-pointer register cache (see [OPTIMIZATION.md](OPTIMIZATION.md)),
relies on `restrict`'s *scope-wide* non-aliasing promise; `malloc` only gives
*point-wise* freshness at the instant of return, so feeding it into that cache
would require the same missing dataflow analysis. Wiring `is_malloc` in is
tracked as low-priority future work, gated on a memory-dependency pass being
added first.

#### `__attribute__((designated_init))` / `[[gnu::designated_init]]`

Marks a struct type as requiring **every** initializer of that type to use
designated (`.field = value`) syntax rather than positional. This guards
against silent breakage when a struct's field order changes — the classic
use case is Linux-kernel-style ABI-facing structs.

This is a **static, parse-time-only** check over braced initializer lists: any
positional element reaching a `designated_init` struct's member list warns,
including the positional tail of a mixed literal (`{.a = 1, 2}`) and `{0}`.
A brace-less copy-initializer (`struct S a = b;`) is not a positional element
list and is never flagged. C23 empty-init `{}` supplies no elements and is
also silent.

```c
struct point { int x, y; } __attribute__((designated_init));

int main(void) {
    struct point a = {.x = 1, .y = 2};  // ok
    struct point b = {1, 2};            // warns: positional initialization
    struct point c = {.x = 1, 2};       // warns: positional tail
    struct point d = {0};               // warns: positional
    struct point e = {};                // ok: no elements
}
```

Diagnosed under `-Wdesignated-init`. Unlike GCC, this is **opt-in only** — it
is not part of `-Wall`/`-Wextra`, since CCCC otherwise enables no warnings by
default; pass `-Wdesignated-init` explicitly (or promote it with
`-Werror=designated-init`).

---

### Parsed but Ignored

Any GNU `__attribute__` identifier that is not explicitly handled (i.e., not `packed`, `aligned`, `unused`/`__unused__`, `deprecated`/`__deprecated__`, or `warn_unused_result`/`__warn_unused_result__`) is **consumed and emits a `-Wattributes` warning**. The parser skips the attribute name and any parenthesised argument list, then continues.

Similarly, any C23 `[[...]]` attribute other than `maybe_unused`, `deprecated`, `nodiscard`, `fallthrough`, or `noreturn` is **consumed and emits a `-Wattributes` warning**.

Ignored attributes include (but are not limited to):

| Attribute | Syntax | Tracking |
|-----------|--------|----------|
| `no_unique_address` | C23 | Parsed but ignored — VM optimisation deferred |
| `visibility` | GNU | Recognized by `__has_attribute` (no ELF, so no-op) — [#657](https://todo.sr.ht/~takeiteasy/cccc/657) |
| `section` | GNU | Recognized by `__has_attribute` (no object-file sections) — [#657](https://todo.sr.ht/~takeiteasy/cccc/657) |
| `weak` | GNU | Recognized by `__has_attribute` (no linker) — [#657](https://todo.sr.ht/~takeiteasy/cccc/657) |
| `weakref` | GNU | |
| `alias` | GNU | Recognized by `__has_attribute` (no linker) — [#657](https://todo.sr.ht/~takeiteasy/cccc/657) |
| `target` | GNU | Recognized by `__has_attribute` (no per-function ISA codegen) — [#657](https://todo.sr.ht/~takeiteasy/cccc/657) |
| `hot` / `cold` | GNU | Recognized by `__has_attribute` (no branch-temperature layout) — [#657](https://todo.sr.ht/~takeiteasy/cccc/657) |
| `always_inline` / `flatten` / `noinline` | GNU | Recognized by `__has_attribute` (no inliner in the JIT) — [#657](https://todo.sr.ht/~takeiteasy/cccc/657) |
| `used` | GNU | Recognized by `__has_attribute` (no symbol-level DCE) — [#657](https://todo.sr.ht/~takeiteasy/cccc/657) |
| `may_alias` | GNU | Recognized by `__has_attribute` (no strict-aliasing optimizer) — [#657](https://todo.sr.ht/~takeiteasy/cccc/657) |
| `mode` | GNU | Recognized by `__has_attribute` (no machine-mode type system) — [#657](https://todo.sr.ht/~takeiteasy/cccc/657) |
| `transparent_union` | GNU | Recognized by `__has_attribute` (no union-arg coercion modeling) — [#657](https://todo.sr.ht/~takeiteasy/cccc/657) |
| `alloc_align` | GNU | Recognized by `__has_attribute` (no alignment-fact propagation) |
| `format_arg` | GNU | |
| `unsequenced` | C23 | |
| `reproducible` | C23 | |

### Open Tickets

| # | Attribute | Priority | Description |
|---|-----------|----------|-------------|
| [#215](https://todo.sr.ht/~takeiteasy/cccc/215) | Catch-all | medium | Remaining GNU builtins and attributes |
| [#657](https://todo.sr.ht/~takeiteasy/cccc/657) | 14 architecturally-inert GNU attributes | low | Register in `known_attrs[]` for `__has_attribute` (done) |

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
| `__builtin_classify_type(expr)` | Small integer classifying `expr`'s type (gcc's `typeclass.h` codes where a matching CCCC type exists — `1`=integer, `2`=char, `3`=enum, `4`=bool, `5`=pointer, `8`=real, `9`=complex, `10`=function, `12`=struct, `13`=union, `14`=array; `99`=vector, CCCC-specific, no gcc equivalent). `expr` is **not** evaluated (matches `sizeof`/gcc semantics) — only its type is used |
| `__builtin_choose_expr(c, e1, e2)` | Compile-time select: `e1` if constant `c` is non-zero, else `e2`. Result carries the chosen arm's type; the unchosen arm is parsed but not evaluated |
| `__builtin_reg_class(type)` | `0` = integer/pointer, `1` = float, `2` = other |
| `__builtin_expect(expr, hint)` | Returns `expr`; `hint` is a branch-prediction hint (ignored) |
| `__builtin_expect_with_probability(expr, hint, prob)` | Returns `expr`; `hint` and `prob` are branch-prediction hints (ignored) |
| `__builtin_prefetch(addr, [rw], [locality])` | Cache prefetch hint; `addr` is evaluated for side effects, hint args are ignored |
| `__builtin_assume(expr)` | Optimizer hint; `expr` is **not** evaluated (matches Clang/GCC semantics) |
| `__builtin_offsetof(type, member)` | Compile-time offset of `member` within `type` |

#### Memory and Control Flow

| Builtin | Description |
|---------|-------------|
| `__builtin_alloca(size)` | Dynamically allocate `size` bytes on the stack |
| `__builtin_alloca_with_align(size, align)` | Like `__builtin_alloca`; `align` is in bits and must be a constant. Only 16-byte alignment is guaranteed; finer alignment is accepted but not enforced. |
| `__builtin_frame_address(0)` | Returns the current frame's base pointer (level 0 only) |
| `__builtin_return_address(n)` | Returns the return address `n` frames up the call stack (all levels supported). The value is a VM bytecode offset (`Pc`/`uint32_t`) cast to `void*`, **not** a host machine address — unlike `__builtin_frame_address`, which returns a real `bp` pointer. Returns `NULL` past the outermost frame. Implemented via the `RETADDR` VM opcode, which walks the saved-bp chain with runtime bounds-checking. **CALLT interaction**: when a function is reached via a tail call (`CALLT`, enabled at `-O1`), the intermediate frame has been unwound; `__builtin_return_address(0)` therefore returns the *original caller's* address, not the address of the tail-call site. See [VM.md](VM.md) (Tail-Call Optimisation section) for details. |
| `__builtin_pc_function_name(pc)` | Maps a VM bytecode offset (`void*` as returned by `__builtin_return_address`) to the name (`const char*`) of the enclosing C function. Returns `NULL` if `pc` is `NULL` or outside all known function ranges. Works in all builds — does **not** require `-g`. |
| `__builtin_pc_source_location(pc, &file, &line)` | Maps a VM bytecode offset to a source file name and 1-based line number. On success sets `*file` (a `const char*` pointing into the VM's internal table) and `*line`, and returns `1`. On failure (unknown pc or source map not populated) sets `*file = NULL`, `*line = 0`, and returns `0`. Requires the program to have been compiled with `-g`; without it this always returns `0`. Use together with `__builtin_return_address` for a full symbolization pipeline. |
| `__builtin_object_size(ptr, type)` | Compile-time object-size computation for statically-known objects (local/global arrays, scalars, struct members reachable via constant-offset chains). Returns exact remaining byte count for `type` 0–3 (bit 0: whole object vs nearest subobject; bit 1: max vs min fallback). Ternary expressions (`cond ? a : b`) are resolved by computing the size for each branch independently and returning the larger (type 0/1) or smaller (type 2/3). Union member accesses report the whole-union size for `type` 0/2 and the specific member size for `type` 1/3. **Heap allocations**: a pointer initialized directly from a call to a function declared `__attribute__((alloc_size(...)))` (see [Attributes](#attributes) above) with compile-time constant size argument(s) resolves to the real allocation size, *provided* the pointer is never reassigned or has its address taken anywhere in the enclosing function — this is checked across the whole function body (including loop back-edges) before the query is resolved, so it can't fold a stale size for a pointer that is reassigned later in the source. Recognition is attribute-driven, not name-based: libc's `malloc`/`calloc`/`realloc`/`reallocarray`/`aligned_alloc` self-describe via `alloc_size` in `include/stdlib.h`, and any annotated custom allocator (e.g. an arena/pool wrapper) participates the same way — a function that merely shares an allocator's name but carries no attribute is correctly left untracked. **Interior heap pointers**: an offset written inline in the builtin's argument, e.g. `__builtin_object_size(p + 32, 0)`, resolves to `alloc_size - 32` when the offset is a compile-time constant and `p` is an alloc-tracked, unpoisoned base pointer — the offset rides the same deferred reassignment/address-of poisoning check as the base pointer itself, so it stays sound across later reassignment (including loop back-edges). The same tracking extends to an interior pointer captured in an intermediate variable, e.g. `char *q = p + 32;` followed by `__builtin_object_size(q, 0)` — `q` records a link to its base pointer and offset at declaration time (chains of such derivations, e.g. a further `char *r = q + 8;`, are followed transitively), and the whole chain must be unpoisoned (every pointer in it single-assignment and never address-taken, for the *entire* enclosing function — including a reassignment of the base pointer that happens *before* the derived variable's own declaration) for the query to resolve; any poisoning anywhere in the chain — of the derived variable itself or of any ancestor it was derived from — keeps it conservative. An offset past the end of the allocation falls back to the conservative default rather than clamping to 0. Any other pattern (non-constant size or offset, reassignment, `&ptr`, an allocator with no `alloc_size` attribute) falls back conservatively to `(size_t)-1` (type 0/1) or `0` (type 2/3) — preserving `_FORTIFY_SOURCE` safety. |
| `__builtin_dynamic_object_size(ptr, type)` | Runtime object-size query. For statically-known objects (stack/global arrays, constant-offset chains, ternary expressions where both branches are statically resolvable) the result is folded to a compile-time constant (identical to `__builtin_object_size`). **Heap** (`malloc`/`calloc`/`realloc`/`reallocarray`, and `alloca`/VLA buffers — both lower to the `MALC` VM-heap opcode and so carry a full `AllocHeader`): the `DYNOBJSZ` opcode binary-searches the `sorted_allocs` base-address table for the containing allocation and returns `AllocHeader.requested_size - offset` — this works for both base pointers and **interior pointers** (`p + k`), resolving to the exact remaining byte count. `--vm-heap` (`-V`, on by default, #665) must be enabled for `malloc`/`calloc`/`realloc`/`reallocarray` to route through the VM heap; `alloca`/VLA always do. **Stack**: an escaping fixed-size stack array/struct/union — one whose address has been passed somewhere the compiler can't statically resolve, e.g. through a function parameter — resolves via `vm->stack_intervals`, the stack analogue of `sorted_allocs` (#675): the compiler tags the local's `[base, base+size)` extent (`STKTAG`) with its creating frame's epoch, and `DYNOBJSZ` trusts a match only while that frame's epoch is still live. Using the builtin at all activates this epoch/interval bookkeeping, independently of `--dangling-detection` (`-1`/`-2`/`-3`). The `type` argument has the same bit encoding as `__builtin_object_size`. **CALLT interaction**: a pointer to a local passed into a *tail-called* function (`CALLT`, `-O1`+) resolves conservatively even though the pointee is still within the same source-level call — the tail call reuses the caller's stack frame for the callee immediately, so the caller's epoch (and its locals' storage) is retired before the callee runs; write the call in non-tail-call form (e.g. assign the argument-holding pointer's use to a local before returning) if this matters. **Limitations**: pointers past the end of the requested allocation (e.g. into 8-byte alignment padding), and any pointer whose owning stack frame has already returned (dangling) or whose provenance cannot be resolved at all, fall back to the conservative value, preserving `_FORTIFY_SOURCE` safety. |
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
| `<errno.h>` | ✓ | `errno` aliases the host's real per-thread errno via an accessor function (`#define errno (*__cccc_errno_ptr())`, same pattern as `stdin`/`stdout`/`stderr` in `<stdio.h>`), so a failing host-backed call (`access`, `sysconf`, etc.) is actually observable from guest code. Windows targets keep the old plain-global behavior (untested target; not wired to the accessor). |
| `<float.h>` | ✓ | Correct single-precision (`FLT_*`) values distinct from `DBL_*`/`LDBL_*` (`long double` is genuinely 64-bit on CCCC, so `LDBL_* == DBL_*` there is accurate, not a bug); full C11 (`DECIMAL_DIG`, `FLT_EVAL_METHOD`, `*_HAS_SUBNORM`, `*_TRUE_MIN`, `*_DECIMAL_DIG`) and C23 (`*_NORM_MAX`, `*_SNAN`) macro set |
| `<limits.h>` | ✓ | |
| `<locale.h>` | ✓ | Host locale APIs registered |
| `<math.h>` | ✓ | Full C99 function set registered; C23 IEC 60559:2020 interchange/classification functions (`fmaximum`/`fminimum` family, `totalorder`/`totalordermag`, `canonicalize`, `getpayload`/`setpayload`/`setpayloadsig`, `llogb`, `fromfp`/`ufromfp`/`fromfpx`/`ufromfpx`, `issignaling`/`iseqsig`/`iscanonical`) implemented as software bit-pattern functions, not FFI -- several are absent from Darwin's libm entirely and glibc only gained `fmaximum`/`fminimum` in 2.35; `isnan`/`isinf`/`signbit`/`fpclassify`/`isnormal`/`isfinite` are real bit-pattern functions dispatched via `_Generic` (`isnan`/`isinf` used to be undefined identifiers referenced by other macros, and `signbit`/`fpclassify`/`isnormal` were semantically wrong -- e.g. `signbit(-0.0)` was false) |
| `<setjmp.h>` | ✓ | CCCC-specific implementation for VM calling convention |
| `<signal.h>` | ✓ | Full POSIX signal set, `#ifdef __APPLE__`-guarded per-platform values (Darwin and Linux signal numbers diverge past the common 1-15 core; e.g. `SIGBUS`/`SIGUSR1`/`SIGCHLD` differ) including job-control (`SIGSTOP`, `SIGCONT`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU`), resource-limit (`SIGXCPU`, `SIGXFSZ`), and misc (`SIGPROF`, `SIGSYS`, `SIGVTALRM`, `SIGIO`, `SIGURG`, `SIGWINCH`) signals. `signal`/`raise` are VM-managed (VSIGNAL/VRAISE opcodes); `sigaction` reuses the same VM-managed slot + async-safe host shim, so it is safe to hand a real guest handler (previously a raw host passthrough that crashed on delivery -- the handler value was never a real callable host pointer). `sa_handler`/`sa_sigaction` share the same union member, and `sa_mask`/`sa_flags` round-trip faithfully through `oact`; `SA_SIGINFO` is honored at dispatch. `sa_mask`/`SA_NODEFER`/`SA_RESETHAND` are enforced in the VM (#787): a handler's `sa_mask` (plus the handler's own signal, unless `SA_NODEFER`) is blocked for the duration of its execution -- a blocked signal stays pending and is delivered once unblocked, whether the block expires because the handler returned (detected via an sp watermark recorded at handler entry -- see the `SigFrame` comment in `src/cccc.h` for why this is a watermark rather than a dedicated opcode, since guest handlers run from the dispatch loop, not native signal context) or was never blocked; `SA_RESETHAND` resets the disposition to `SIG_DFL` after one delivery, at either delivery site, before the handler runs. `signal()`/`VSIGNAL` clear `sa_mask`/`sa_flags` on the slot they touch, so a stale `SA_RESETHAND`/`SA_SIGINFO` from a prior `sigaction()` call can't leak into a later `signal()` registration. `SA_RESTART` is passed through to the real host `sigaction()` instead of emulated in the VM -- it's a kernel-level concern for blocking FFI syscalls (`read`/`pause`/etc.) that the dispatch loop can't meaningfully act on mid-syscall; `SA_NOCLDSTOP`/`SA_NOCLDWAIT` are passed through the same way. Guest code can register the three-argument `sa_sigaction(int, siginfo_t *, void *)` form via `SA_SIGINFO`: a real delivered signal (e.g. `SIGCHLD`) captures genuine kernel siginfo data (`si_code`/`si_pid`/`si_uid`/`si_status`) at the moment of delivery, while `raise()` -- which never goes through the host signal mechanism -- synthesizes POSIX `raise()` semantics (`si_code == SI_USER`, `si_pid`/`si_uid` = self); the ucontext argument is always `NULL` (not modelled). `SA_*` flag values (including the new `SA_SIGINFO`) are `#ifdef __APPLE__`-guarded per-platform, since e.g. real Linux `SA_SIGINFO` (`4`) collides with macOS's `SA_RESETHAND` (`0x0004`). `sigemptyset`/`sigfillset`/`sigaddset`/`sigdelset`/`sigismember` operate natively on the guest's own 4-byte `sigset_t` representation rather than the host's real `sigset_t` (previously a raw passthrough too -- harmless on macOS, where the real `sigset_t` also happens to be 4 bytes, but a 128-byte out-of-bounds write/read on Linux, where it isn't). Handlers run from the dispatch loop, never native signal context. The macOS crash dispatcher preserves guest dispositions while trapping default `SIGSEGV`/`SIGBUS`/`SIGFPE`/`SIGILL`/`SIGABRT` into an interactive debugger. `SIGTRAP` with `-g` breaks into the debugger. `siginfo_t` (`si_signo`/`si_errno`/`si_code`/`si_pid`/`si_uid`/`si_status` plus trailing padding sized to the full host struct -- 104 bytes macOS, 128 bytes Linux -- so host `waitid()` can't overflow adjacent guest memory), `union sigval`, `SI_USER`, and `CLD_*` si-codes are declared for `waitid()` (see `<sys/wait.h>`) and `sa_sigaction` |
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
| `<fenv.h>` | ✓ | `FE_*` constants and `fexcept_t`/`fenv_t` sizes are injected from the real host `<fenv.h>` this binary was compiled against (not hardcoded), so rounding-mode and exception-flag calls are correct on whatever platform is running; `FLT_ROUNDS` (`<float.h>`) tracks the dynamic mode via `fegetround()`. `#pragma STDC FENV_ACCESS` / `FP_CONTRACT` are accepted and ignored (no scoped in-source toggle; `FP_CONTRACT` behaves correctly by default since contraction only happens when `--fma` is passed). Float-to-integer conversion (`(long long)some_double`, both explicit casts and implicit ones from constant folding) is defined and saturating rather than a bare UB host cast: NaN → 0, out-of-range → `LLONG_MIN`/`LLONG_MAX` matching the sign, `FE_INVALID` raised in all three cases |
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
| `aligned_alloc` | ✓ | Routed through the VM heap (`MALCA` opcode, #668) when the VM heap is enabled (the default); backed by host aligned allocation only under `-V`/`--vm-heap` |
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
| `exp10`, `sinpi`/`cospi`/`tanpi`, `asinpi`/`acospi`/`atanpi`/`atan2pi` (+ `f`/`l`) | ✓ | `double`/`float`/`long double` variants all correct ([#406](https://todo.sr.ht/~takeiteasy/cccc/406), the float-FFI limitation this table used to note, is resolved) |
| `mbrtoc8`, `c8rtomb` (`<uchar.h>`) | ✓ | Full incremental state machine per §7.31.1 (one `char8_t` per call, `(size_t)-3` queued-byte convention) |
| `printf`/`scanf` family `%b`/`%B` (binary integer) specifier | ✓ | macOS / glibc < 2.35: handled by the custom `format_printf.c`/`format_scanf.c` engines; glibc 2.35+ uses the native host implementation |
| IEC 60559:2020 interchange functions (`<math.h>`) | ✓ | `fmaximum`/`fminimum`/`fmaximum_num`/`fminimum_num`/`fmaximum_mag`/`fminimum_mag`/`fmaximum_mag_num`/`fminimum_mag_num`, `totalorder`/`totalordermag`, `canonicalize`, `getpayload`/`setpayload`/`setpayloadsig`, `llogb`, `fromfp`/`ufromfp`/`fromfpx`/`ufromfpx`, `issignaling`/`iseqsig`/`iscanonical` (`double`/`float`/`long double` where applicable); software bit-pattern implementations, not FFI (Darwin's libm has none of `totalorder`, `fromfp`/`ufromfp`, `getpayload`/`setpayload`, or `llogb`; glibc only gained `fmaximum`/`fminimum` in 2.35) |

---

### POSIX Headers

POSIX headers are embedded and backed by host OS calls. They are only available on POSIX targets (not Windows). They are always-on and ungated by feature-test macros.

CCCC predefines the common POSIX feature-test macros — `_POSIX_C_SOURCE`
(`200809L`), `_POSIX_SOURCE` (`1`), `_XOPEN_SOURCE` (`700`), and
`_DEFAULT_SOURCE` (`1`) — so third-party code that feature-tests before
including a POSIX header (e.g. `#if !defined(_XOPEN_SOURCE)`) sees the
POSIX.1-2008 / X/Open 7 surface CCCC actually exposes, rather than always
seeing nothing. A user `-D`/`#define` of any of these overrides the default
silently (predefined macros are not redefinition-locked).

`sysconf`/`pathconf`/`fpathconf`/`confstr` use CCCC's own canonical,
host-independent numbering for `_SC_*`/`_PC_*`/`_CS_*` (defined in
`include/unistd.h`), translated to the host's real numbering by wrapper
functions in `src/stdlib/posix.c` before the host call. This keeps compiled
`.c4` bytecode portable across hosts whose libc disagree on these numbers
(e.g. macOS vs glibc). `sysconf(_SC_VERSION)`, `_SC_2_VERSION`, and
`_SC_XOPEN_VERSION` answer with CCCC's own VM-model constants rather than the
host's; an unrecognized or host-unsupported name returns `-1`/`0`.

A handful of host libc globals (`errno`, and getopt's `optarg`/`optind`/
`opterr`/`optopt`) are exposed the same way `stdin`/`stdout`/`stderr` always
have been: as a macro expanding to a dereferenced accessor-function call
(`#define errno (*__cccc_errno_ptr())`) rather than a plain `extern` global.
Ordinary compiled globals live in the VM's own data segment and have no
connection to identically-named host process state, so a plain `extern int
errno;` would silently stay zero forever regardless of what host-backed calls
actually did. The accessor pattern makes these specific, known globals alias
the host's real storage directly, so guest code observes the real outcome.

| Header | Status | Notes |
|---|---|---|
| `<arpa/inet.h>` | ✓ | Network byte-order conversion (`htonl`, `htons`, `ntohl`, `ntohs`), address manipulation (`inet_addr`, `inet_ntoa`, `inet_ntop`, `inet_pton`) |
| `<dirent.h>` | ✓ | Directory entry iteration (`opendir`, `readdir`, `readdir_r`, `closedir`, `seekdir`, `telldir`, `rewinddir`, `alphasort`, `scandir`, `DIR`, `struct dirent`) |
| `<dlfcn.h>` | ✓ | VM-managed dynamic loading (`dlopen`, `dlsym`, `dlclose`, `dlerror`); `dlsym` function symbols are callable through typed function pointers for scalar/pointer signatures |
| `<fcntl.h>` | ✓ | File control (`open`, `creat`, `fcntl`), `O_*` (including `O_DIRECTORY`, `O_NOFOLLOW`, `O_SYNC`, `O_NOCTTY`, `O_DSYNC`, plus Linux `O_RSYNC`) and `S_*` permission constants, record-locking `F_*` commands (including `F_GETOWN`/`F_SETOWN`), `FD_CLOEXEC`, `struct flock`; `fallocate` declared and registered under `__linux__`. The Linux aarch64 `O_DIRECTORY`/`O_NOFOLLOW` values have been empirically verified against a real header on native aarch64 (Colima VM) |
| `<fnmatch.h>` | ✓ | Filename pattern matching (`fnmatch`, `FNM_*` constants) |
| `<getopt.h>` | ✓ | Command-line option parsing (`getopt`, `getopt_long`, `struct option`); `optarg`/`optind`/`opterr`/`optopt` alias the host's real getopt state via accessor functions (same pattern as `<errno.h>`'s `errno`) so they reflect what the host parser actually found |
| `<glob.h>` | ✓ | Pathname globbing (`glob`, `globfree`, `glob_t`, `GLOB_*` constants); a real guest `errfunc` callback is safe to pass (previously a raw host passthrough that crashed if `errfunc` was non-NULL) |
| `<grp.h>` | ✓ | Group database (`getgrgid`, `getgrnam`, `getgrgid_r`, `getgrnam_r`, `struct group`) |
| `<libgen.h>` | ✓ | Pathname manipulation (`basename`, `dirname`) |
| `<net/if.h>` | ✓ | Interface name/index resolution (`if_nametoindex`, `if_indextoname`, `if_nameindex`, `if_freenameindex`, `struct if_nameindex`, `IF_NAMESIZE`/`IFNAMSIZ`) (#788), needed to target a specific interface for `<netinet/in.h>` IPv6 multicast options (`ipv6mr_interface`, `IPV6_MULTICAST_IF`) instead of relying on interface index 0. Layout and constants identical on macOS and Linux x86_64/aarch64 (verified). `if_nameindex()`'s return value and its `if_name` strings are host-allocated; the guest must release them via `if_freenameindex()`, never `free()` |
| `<netdb.h>` | ✓ | Network database (`gethostbyname`, `gethostbyaddr`, `getaddrinfo`, `freeaddrinfo`, `getnameinfo`, `getnetbyname`, `getnetbyaddr`, `setnetent`, `endnetent`, `struct hostent`, `struct addrinfo`, `struct netent`), `getservbyname`/`getservbyport`/`setservent`/`endservent` (`struct servent`, `s_port` in network byte order) and `getprotobyname`/`getprotobynumber`/`setprotoent`/`endprotoent` (`struct protoent`), `EAI_*` and `NI_*` constants. `gethostbyname`/`gethostbyaddr`/`getaddrinfo`/`getnameinfo`/`getnetbyname`/`getnetbyaddr` release the VM GIL while blocked, since real DNS/NSS lookups can take seconds; `gethostbyname`/`gethostbyaddr`/`getnetbyname`/`getnetbyaddr` return pointers into static, non-reentrant host storage, so concurrent guest threads calling them can race on that shared buffer (inherent to the POSIX API -- `getaddrinfo`/`getnameinfo` are reentrant and unaffected). Race-free alternative (#785): `gethostbyname_r`/`gethostbyaddr_r`/`getnetbyname_r` (plus `HOST_NOT_FOUND`/`TRY_AGAIN`/`NO_RECOVERY`/`NO_DATA`/`NO_ADDRESS`) copy the result into the caller's own buffer under a shared mutex that also now guards the plain lookups above, so the static buffer is never written concurrently -- a portable shim rather than a passthrough to the host's own `_r` functions, since macOS has none of these three (glibc-only extensions); there is no standard `getnetbyaddr_r` on any platform |
| `<netinet/in.h>` | ✓ | Internet address family (`struct sockaddr_in`, `struct in_addr`, `in_port_t`, `in_addr_t`, `INADDR_*`, `IPPROTO_*` (including `IPPROTO_ICMP`/`IPPROTO_ICMPV6`/`IPPROTO_IPV6`/`IPPROTO_RAW`), `IPPORT_RESERVED`); IPv6 (`struct sockaddr_in6`, `struct in6_addr`, `IN6ADDR_ANY_INIT`/`IN6ADDR_LOOPBACK_INIT`, `IPV6_V6ONLY`, `IPV6_UNICAST_HOPS`, `IPV6_MULTICAST_HOPS`/`_LOOP`/`_IF`, `IPV6_JOIN_GROUP`/`_LEAVE_GROUP`), `sockaddr_in6` following the same per-platform `sin6_len`/1-byte-`sa_family_t` (Apple) vs no-`sin6_len`/2-byte-`sa_family_t` (Linux) layout as `sockaddr_in`. Advanced IPv6 options (`IPV6_PKTINFO`/`_RECVPKTINFO`, `IPV6_TCLASS`/`_RECVTCLASS`, `IPV6_CHECKSUM`, `IPV6_DONTFRAG`, `IPV6_HOPLIMIT`/`_RECVHOPLIMIT`, `IPV6_HOPOPTS`/`_DSTOPTS` and their `RECV*` counterparts, `IPV6_RTHDR`/`_RECVRTHDR`/`_RTHDR_TYPE_0`) plus `struct ipv6_mreq` (for a real `IPV6_JOIN_GROUP`/`_LEAVE_GROUP` multicast membership, not just constant distinctness) and `struct in6_pktinfo` -- values diverge per platform (verified against real macOS and Linux x86_64/aarch64 headers) and require `__APPLE_USE_RFC_3542` to be defined before the real macOS header is included (done internally by `src/stdlib/posix.c`). A real `IPV6_JOIN_GROUP` multicast send/receive round trip (`ff02::1`, scoped via `<net/if.h>`) is asserted, not just tolerated, on macOS and Linux x86_64/aarch64 (#788) -- on the Linux containers used to verify this, `lo` can join the group but has no multicast route (`ENETUNREACH` on send), while `eth0` completes the round trip, so the test walks every interface reported by `if_nameindex()` rather than assuming loopback |
| `<poll.h>` | ✓ | Event polling (`poll`, `struct pollfd`, `nfds_t`, `POLL_*` constants) |
| `<pthread.h>` | ~ | POSIX pthread lifecycle, mutex (including `pthread_mutexattr_init/destroy/settype/gettype` and `PTHREAD_MUTEX_RECURSIVE`/`ERRORCHECK`/`NORMAL`/`DEFAULT`), condition-variable, TLS key, and basic attr APIs are backed by host pthreads. VM bytecode execution is serialized by a recursive GIL, so pthreads provide correctness and blocking/wakeup semantics, not parallel VM execution. |
| `<pwd.h>` | ✓ | Password database (`getpwuid`, `getpwnam`, `getpwuid_r`, `getpwnam_r`, `struct passwd`) |
| `<regex.h>` | ✓ | Regular expression matching (`regcomp`, `regexec`, `regerror`, `regfree`, `regex_t`, `regmatch_t`) |
| `<strings.h>` | ✓ | BSD string functions (`strcasecmp`, `strncasecmp`, `bzero`, `bcopy`, `bcmp`, `index`, `rindex`) |
| `<sys/file.h>` | ✓ | Advisory file locking (`flock`, `LOCK_SH`, `LOCK_EX`, `LOCK_NB`, `LOCK_UN`) |
| `<sys/ioctl.h>` | ✓ | Device control (`ioctl`, `struct winsize`, `TIOCGWINSZ`, `TIOCSWINSZ`) |
| `<sys/mman.h>` | ✓ | Memory management (`mmap`, `munmap`, `mprotect`, `msync`, `posix_madvise`, `mlock`, `munlock`, `mlockall`, `munlockall`, `shm_open`, `shm_unlink`), `PROT_*`, `MAP_*`, `MAP_FAILED`, `MS_*`, `MADV_*`, `MCL_CURRENT`/`MCL_FUTURE`, `MAP_NORESERVE` (both platforms, differing values) constants; `mremap`, `MCL_ONFAULT`, `MAP_LOCKED`/`MAP_POPULATE` declared and registered under `__linux__` (macOS lacks these) |
| `<sys/mount.h>` | ✓ | Filesystem statistics (`statfs`, `fstatfs`, `struct statfs`) — minimal portable field set |
| `<sys/param.h>` | ✓ | System limits and helpers (`MAXPATHLEN`, `NBBY`, `MIN`, `MAX`) |
| `<sys/socket.h>` | ✓ | Socket API (`socket`, `socketpair`, `bind`, `listen`, `accept`, `connect`, `setsockopt`, `getsockopt`, `getsockname`, `getpeername`, `sockatmark`, `shutdown`, `struct sockaddr`, `socklen_t`), data transfer (`recv`, `send`, `recvfrom`, `sendto`, `recvmsg`, `sendmsg`), `AF_UNIX`/`AF_LOCAL`, expanded `SO_*` (`SO_ERROR`, `SO_KEEPALIVE`, `SO_BROADCAST`, `SO_RCVBUF`, `SO_SNDBUF`, `SO_LINGER`, `SO_TYPE`, `SO_REUSEPORT`, ...) and `MSG_*` (`MSG_PEEK`, `MSG_DONTWAIT`, `MSG_WAITALL`, `MSG_OOB`, `MSG_EOR`, `MSG_TRUNC`, `MSG_CTRUNC`, plus Linux `MSG_NOSIGNAL`) constants; `SOCK_CLOEXEC`/`SOCK_NONBLOCK` declared under `__linux__` (macOS lacks these). `struct msghdr`/`cmsghdr` and the `CMSG_FIRSTHDR`/`CMSG_NXTHDR`/`CMSG_DATA`/`CMSG_SPACE`/`CMSG_LEN`/`CMSG_ALIGN` macros support `SCM_RIGHTS` ancillary-data fd-passing over `sendmsg`/`recvmsg`; `msg_iovlen`/`msg_controllen`/`cmsg_len` are `socklen_t`/`int` (4 bytes) on macOS vs `size_t` (8 bytes) on Linux, and `CMSG_ALIGN` rounds to 4 bytes (macOS) vs 8 bytes (64-bit Linux) to match `sizeof(struct cmsghdr)` == 12/16 respectively |
| `<sys/stat.h>` | ✓ | File status (`stat`, `fstat`, `lstat`, `fstatat`, `chmod`, `fchmod`, `fchmodat`, `mkdir`, `mkdirat`, `mkfifo`, `mknod`, `umask`), `struct stat`, `S_*` constants and macros, `UTIME_NOW`/`UTIME_OMIT`, `AT_FDCWD`/`AT_SYMLINK_NOFOLLOW`/`AT_REMOVEDIR` |
| `<sys/resource.h>` | ✓ | Resource usage (`getrusage`), `struct rusage` (identical layout on macOS and Linux), `RUSAGE_SELF`/`RUSAGE_CHILDREN`/`RUSAGE_THREAD` (Linux-only). `ru_maxrss` is measured in bytes on macOS but kilobytes on Linux -- a real semantic divergence, not normalized by this header. Resource limits (#786): `struct rlimit`, `getrlimit`/`setrlimit`, `RLIMIT_*` -- unlike `struct rusage`'s layout, the `RLIMIT_*` *numbering* genuinely diverges between macOS and Linux (verified against real headers); notably `RLIMIT_AS` aliases `RLIMIT_RSS` on macOS but is distinct on Linux, and `RLIM_INFINITY` differs (`INT64_MAX`-as-unsigned on macOS vs `UINT64_MAX` on Linux). Process priority: `getpriority`/`setpriority`, `PRIO_PROCESS`/`PRIO_PGRP`/`PRIO_USER` (identical on both platforms) |
| `<sys/time.h>` | ✓ | Time operations (`gettimeofday`, `settimeofday`, `utimes`, `futimes`, `lutimes`, `setitimer`, `getitimer`), `struct timeval`, `struct timezone`, `struct itimerval`, `ITIMER_REAL`/`ITIMER_VIRTUAL`/`ITIMER_PROF`, `timeradd`, `timersub` |
| `<sys/types.h>` | ✓ | Basic system types (`dev_t`, `ino_t`, `mode_t`, `nlink_t`, `uid_t`, `gid_t`, `off_t`, `pid_t`, `blksize_t`, `blkcnt_t`, `useconds_t`, `sa_family_t`, `socklen_t`, `clockid_t`, `key_t`, `id_t`, `fsblkcnt_t`, `fsfilcnt_t`, `nl_item`); `timer_t` declared under `__linux__` only -- macOS does not implement the POSIX `timer_create`/`timer_settime` API this type belongs to |
| `<sys/un.h>` | ✓ | Unix domain socket addresses (`struct sockaddr_un`, platform-correct `sun_path` length and `sun_len`/no-`sun_len` layout) |
| `<sys/wait.h>` | ✓ | Process wait (`wait`, `waitpid`, `waitid`, `wait3`, `wait4`), `idtype_t` (`P_ALL`/`P_PID`/`P_PGID`), `WNOHANG`, `WUNTRACED`, `WCONTINUED`, `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`, `WIFSTOPPED`, `WIFCONTINUED`, `WSTOPSIG`, `WCOREDUMP`, `WEXITED`/`WSTOPPED`/`WNOWAIT` (`waitid()` 4th-argument flags). `wait3`/`wait4` also fill a `struct rusage` (see `<sys/resource.h>`) for the reaped child |
| `<termios.h>` | ✓ | Terminal I/O (`tcgetattr`, `tcsetattr`, `cfgetispeed`, `cfgetospeed`, `cfsetispeed`, `cfsetospeed`, `cfsetspeed`, `cfmakeraw`, `tcdrain`, `tcflow`, `tcflush`, `tcsendbreak`, `struct termios`, `cc_t`, `speed_t`, `tcflag_t`), full `c_iflag`/`c_oflag`/`c_cflag`/`c_lflag` bit sets, all `V*` special-character indices, baud rates through `B230400` -- all platform-split between macOS and Linux (previously several of these were unconditionally macOS-only values silently misapplied on Linux; fixed and empirically verified against real Linux x86_64/aarch64 headers) |
| `<unistd.h>` | ✓ | Core POSIX API (`read`, `write`, `pread`, `pwrite`, `close`, `lseek`, `access`, `unlink`, `rmdir`, `chdir`, `getcwd`, `getpid`, `getppid`, `getuid`, `geteuid`, `getgid`, `getegid`, `seteuid`, `setegid`, `setuid`, `setgid`, `getgroups`, `setgroups`, `initgroups`, `getlogin`, `getlogin_r`, `fchown`, `chown`, `lchown`, `readlink`, `symlink`, `link`, `getpagesize`, `sleep`, `usleep`, `alarm`, `pause`, `nice`, `getpgid`, `setpgid`, `getpgrp`, `setsid`, `getsid`, `fchdir`, `gethostname`, `sethostname`, `ttyname_r`, `readv`, `writev`, `struct iovec`, `pipe`, `fork`, `execv`, `execve`, `execl`, `execlp`, `execle`, `execvp`, `_exit`, `ssize_t`, `STDIN/STDOUT/STDERR_FILENO`, `SEEK_*`, `F_OK`/`R_OK`/`W_OK`/`X_OK`, `sysconf`, `pathconf`, `fpathconf`, `confstr`, canonical `_SC_*`/`_PC_*`/`_CS_*` constants, `_POSIX_VERSION`/`_POSIX2_VERSION`/`_XOPEN_VERSION`; `splice` declared and registered under `__linux__`; `fdatasync` likewise declared and registered only under `__linux__` -- Darwin's libc has no equivalent syscall/library symbol at all, unlike `fsync`) |
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

> **Known limitation ([#407](https://todo.sr.ht/~takeiteasy/cccc/407)):** when a user-defined variadic function forwards its `va_list` to `vprintf`/`vfprintf`/`vsprintf`/`vsnprintf`/`vscanf`/`vfscanf`/`vsscanf`, only the *first* variadic argument is passed through correctly; subsequent arguments are garbage. This is a pre-existing VM/FFI limitation, not specific to `%b`/`%B`.

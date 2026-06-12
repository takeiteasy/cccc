# C Language Coverage

Conformance status for each C standard, plus the GNU and Microsoft extension
syntaxes that CCCC accepts. Intended as a reference for `--std` flag work and
as a checklist of what is currently parsed vs. what is semantically honoured.

Standard library coverage, POSIX headers, built-in functions, and attribute
syntax details have their own documents:

| Doc | Covers |
|-----|--------|
| [STDLIB.md](STDLIB.md) | Standard library headers by C edition, POSIX headers, `__builtin_*` functions |
| [ATTRIBUTES.md](ATTRIBUTES.md) | GNU `__attribute__((...))`, C23 `[[...]]`, `@`-prefix syntax, per-attribute semantics |

| Status | Meaning |
|---|---|
| ✓ | Fully supported |
| ~ | Partial — accepted by the compiler but behaviour is incomplete or approximated |
| ✗ | Not supported |

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
| `volatile` | ✓ | Tracked; no codegen effect ([#311](https://todo.sr.ht/~takeiteasy/cccc/311)) |
| `register` | ~ | Parsed and accepted; ignored |
| `auto` (storage class) | ~ | Parsed and accepted; ignored |
| String literals and concatenation | ✓ | |
| `L"..."` wide string literals | ✓ | Parsed; stored as UTF-32 |
| `L'...'` wide character literals | ✓ | |
| K&R-style function definitions | ✓ | |
| Trigraphs | ✗ | Removed in C23; intentionally not supported |
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
| `restrict` pointers | ~ | Parsed and accepted; aliasing not tracked ([#267](https://todo.sr.ht/~takeiteasy/cccc/267)) |
| `static` array-parameter indices (`void f(int a[static 10])`) | ~ | Parsed and accepted; minimum-size constraint not enforced ([#310](https://todo.sr.ht/~takeiteasy/cccc/310)) |
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
| `_Thread_local` | ~ | Emits `-Wignored-features`; no thread-local storage |
| `_Atomic` types | ~ | Emits `-Wignored-features`; non-atomic load/store emitted |
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
| `constexpr` for objects | ~ | Parsed; constant propagation only — not a full compile-time guarantee |
| `auto` type inference | ✗ | `auto` is parsed as a storage class; C23 type-deduction form not supported |
| `nullptr` keyword | ✗ | |
| `_BitInt(N)` arbitrary-precision integers | ✗ | |
| Binary integer literals `0b10101010` | ✓ | |
| Digit separators `1'000'000` | ✓ | |
| `[[...]]` attributes | ~ | Parsed; see [ATTRIBUTES.md](ATTRIBUTES.md) for per-attribute status |
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
| `__has_c_attribute` | ✓ | Returns `1` for C/CCCC attributes with semantic support |
| `__has_include` | ✓ | Checks CCCC, `-I`, and `-i` include paths |

---

## GNU Extensions

| Feature | Status | Notes |
|---|---|---|
| Statement expressions `({ ... })` | ✓ | |
| `__attribute__((...))` | ~ | Parsed; `aligned`, `packed`, `unused`, `deprecated`, `format`, `nodiscard`, `fallthrough`, `noreturn` supported (see [ATTRIBUTES.md](ATTRIBUTES.md)) |
| Labels as values `&&label` | ✓ | |
| Computed goto `goto *expr` | ✓ | |
| Switch case ranges `case 1 ... 5:` | ✓ | |
| Zero-length arrays `int arr[0]` | ✓ | |
| Nested functions | ✓ | Access to parent-scope variables via static link |
| `__builtin_*` | ✓ | Lowered by the compiler; see [STDLIB.md](STDLIB.md) for the full list |
| `__thread` storage class | ~ | Emits `-Wignored-features`; treated as `static` |
| `__restrict` / `__restrict__` | ~ | Parsed; aliasing not tracked ([#267](https://todo.sr.ht/~takeiteasy/cccc/267)) |
| `__typeof__` | ✓ | Synonym for `typeof` |
| `__asm__` / `asm(...)` inline assembly | ✓ | No-op by default; `--asm-passthru` compiles via native CC and executes via FFI; custom callback via `cc_set_asm_callback` |

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
| `__declspec(restrict)` | ✗ | Lands with [#267](https://todo.sr.ht/~takeiteasy/cccc/267)–[#269](https://todo.sr.ht/~takeiteasy/cccc/269) restrict work |
| `__declspec(safebuffers)` / `selectany` / `code_seg` / `allocate` | ✗ | No-op shims — pending |
| `__declspec(thread)` | ✗ | TLS via VM per-thread storage — pending |
| `__cdecl` / `__stdcall` / `__fastcall` / `__thiscall` / `__vectorcall` | ✗ | Calling-convention keywords, no-op (CCCC has a single VM ABI) — pending |
| `__ptr32` / `__ptr64` / `__sptr` / `__uptr` / `__unaligned` / `__w64` | ✗ | Pointer modifiers, no-op — pending |
| `__forceinline` / `__inline` | ~ | Fold to existing `inline` (dead-elim + inlining applies) — pending `__declspec` alias |
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
| `#pragma cccc comptime` / `#pragma cccc comptime begin` / `#pragma cccc comptime end` | ✓ | Comptime block — see [MACROS.md](MACROS.md#comptime-block) |
| `#include [[cccc::comptime]]` / `#include [[cccc::emit]]` | ✓ | Route includes to the comptime unit or generated-output preamble |
| `#pragma cccc emit begin` / `#pragma cccc emit end` | ✓ | Emit raw preprocessor directives to the generated-output preamble |
| `#pragma comment(lib, "x")` | ✗ | Link hint, no-op — pending |
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

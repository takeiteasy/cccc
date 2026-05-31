# C Language Coverage

Conformance status for each C standard. Intended as a reference for `--std` flag work.

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
| K&R-style function definitions | ✗ | |
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
| `<signal.h>` | ✓ | Common signal constants, `signal`, and `raise` |
| `<stdarg.h>` | ✓ | JCC-specific implementation |
| `<stddef.h>` | ✓ | |
| `<stdio.h>` | ✓ | |
| `<stdlib.h>` | ✓ | |
| `<string.h>` | ✓ | |
| `<time.h>` | ✓ | |

---

## C99

### Language

| Feature | Status | Notes |
|---|---|---|
| `//` single-line comments | ✓ | |
| `long long int` and `unsigned long long int` | ✓ | |
| `_Bool` | ✓ | |
| `_Complex` | ✓ | Native scalar representation with arithmetic, casts, assignment, and equality |
| `_Imaginary` | ~ | Accepted as compatibility spelling for the corresponding complex type |
| Mixed declarations and statements | ✓ | |
| Variable declaration in `for` initialiser | ✓ | |
| Variable-length arrays (VLA) | ✓ | Allocated via VM heap |
| Flexible array members (`struct { int n; int arr[]; }`) | ✓ | |
| Designated initialisers — structs and arrays | ✓ | |
| Compound literals | ✓ | |
| `inline` functions | ~ | Parsed; behaves like `static` — no inlining optimisation |
| `restrict` pointers | ~ | Parsed and accepted; aliasing not tracked |
| `static` array-parameter indices (`void f(int a[static 10])`) | ✗ | |
| `__func__` predefined identifier | ✓ | |
| Variadic macros `__VA_ARGS__` | ✓ | |
| `_Pragma(...)` operator | ✗ | |
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
| `[[...]]` attributes | ~ | Parsed and accepted; all attributes are ignored |
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
| `__attribute__((...))` | ~ | Parsed; all attributes ignored |
| Labels as values `&&label` | ✓ | |
| Computed goto `goto *expr` | ✓ | |
| Switch case ranges `case 1 ... 5:` | ✓ | |
| Zero-length arrays `int arr[0]` | ✓ | |
| Nested functions | ✓ | Access to parent-scope variables via static link |
| `__builtin_expect` | ~ | Parsed; branch-probability hint ignored |
| `__builtin_unreachable` | ✗ | |
| `__builtin_offsetof` | ✓ | |
| `__thread` storage class | ~ | Parsed; treated as `static` |
| `__restrict` / `__restrict__` | ~ | Parsed; aliasing not tracked |
| `__typeof__` | ✓ | Synonym for `typeof` |
| `__asm__` / `asm(...)` inline assembly | ~ | Accepted; executed as a no-op unless a callback emits custom bytecode |

---

## Not Supported

| Feature | Notes |
|---|---|
| Threading (`<threads.h>`, `pthread`) | JCC is single-threaded |
| Atomic operations (`<stdatomic.h>` operations) | Headers present; operations are non-atomic |
| Complex function call ABI | Passing or returning complex values by function call is not implemented |
| Native code generation | JCC produces VM bytecode only |
| Shared-library auto-linking for arbitrary undeclared symbols | `dlfcn.h` calls are available; `--library` opens requested libraries for registered FFI symbols |

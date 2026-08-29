# C Language Coverage

Conformance status for each C standard, plus GNU and Microsoft extension
syntaxes that CCCC accepts, and attribute syntax support. Intended as a
reference for `--std` flag work and as a checklist of what is currently
parsed vs. what is semantically honoured. Standard library and
`__builtin_*` coverage lives in [STDLIB.md](STDLIB.md); how this language
surface is lowered (or refused) under `-c=native` lives in
[NATIVE.md](NATIVE.md).

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

`<pthread.h>` also round-trips under `-c=native`/`-m`/`-c=generated`: `include/
pthread.h` hands off to the real host `<pthread.h>` (#1022), so the compiled
binary calls real host pthread functions directly with no VM/FFI layer in
between — see [Serialized-output divergences](NATIVE.md#serialized-output-divergences)
and [man/HEADERS.md](HEADERS.md) for the header-hand-off mechanics.
Thread-local storage (`_Thread_local`/`__thread`) is also emitted correctly
in serialized output. `<threads.h>` (C11 `thrd_*`/`mtx_*`/`cnd_*`/`tss_*`/
`call_once`) round-trips under `-c=native` too (#1088) — unlike `<pthread.h>`,
this is *not* a `#include_next` hand-off onto a real host `<threads.h>`:
CCCC's own `thrd_error`/`thrd_timedout`/`thrd_busy`/`thrd_nomem` encoding
doesn't match glibc's, and Darwin has no `<threads.h>` at all. Instead a
self-contained shim (`serialize_threads_shims`, `src/serialize_shims.c`) defines
`thrd_*`/`mtx_*`/`cnd_*`/`tss_*`/`call_once` directly over the real host
`<pthread.h>` that's already replayed — the host's own `<threads.h>` is
never consulted on either platform, so both macOS and Linux round-trip from
one change. `call_once` is a real function on both back ends now, not the
guest-side macro it used to be (safe only under the VM's own GIL) — see
[Serialized-output divergences](NATIVE.md#serialized-output-divergences) for the
shim's shape and its residual gaps.

---

## C89 / C90

| Feature | Status | Notes |
|---|---|---|
| `char`, `short`, `int`, `long`, `float`, `double` | ✓ | |
| `unsigned` integer variants | ✓ | |
| `void` | ✓ | |
| Pointers — declaration, `*`, `&`, arithmetic | ✓ | |
| Arrays — fixed-size, multidimensional, initialisation | ✓ | `&a` on a fixed-size array has the standard (non-decayed) type `T (*)[N]`, so `&a + 1` strides the whole array, not one element — mirroring the VLA `&v` shape below (#973/#975). A bare array name used as a value (not under `&`) still decays to an element pointer as usual; only `&a`'s own static type changed |
| Structs — declaration, member access, nested | ✓ | |
| Unions | ✓ | |
| Enums — explicit values, use in expressions and switch | ✓ | |
| Bitfields — signed/unsigned, read-modify-write | ✓ | Bit packing within a struct is always compact (a member starts at the next free bit, crossing into a new storage unit only when it wouldn't fit in the current one), and a member directly following a bit-field run — bit-field or not, including a flexible array member — always starts on the next whole byte, never mid-byte (#1164). A struct's own overall size/alignment additionally rounds up to cover each bitfield member's full declared-type storage unit, matching GCC (`int f : 5;` reserves a whole 4-byte `int` slot, not just 5 bits) — this is a single stated policy (#1170), CCCC follows GCC uniformly, since GCC and clang genuinely disagree on several of these shapes and there is no ABI to settle the tie (both ship on every supported platform). A **nonzero-width unnamed** bit-field (`int : 3;`) contributes exactly like a named one — suppressed by `__attribute__((packed))` and capped by `#pragma pack(N)` the same way (clang instead never counts it; #1176 reversed #1127's original claim that no unnamed member ever contributes). A **width-0** unnamed bit-field (`int : 0;`) is a different rule, not a special case of the above: it contributes its declared type's alignment *unconditionally*, surviving both `packed` and `#pragma pack(N)` (clang never raises alignment for it at all). A bit-field's own explicit `__attribute__((aligned(N)))` — the GNU spelling only, `_Alignas(N)`/`alignas(N)` is rejected on a bit-field with a diagnostic matching GCC's ("alignment specified for bit-field"), and neither compiler accepts a C23 `[[...]]` attribute in that position at all — aligns its storage unit and always raises the struct's own alignment, surviving `packed` (#1163) and capped by `#pragma pack(N)` (#1173) the same way an ordinary member's explicit alignment is, even on an *unnamed* bit-field (#1165; clang never raises alignment there either — CCCC follows GCC). A `packed` struct otherwise keeps the fully compact layout for everything except the width-0 case above |
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
| `switch` / `case` / `default` | ✓ | Duplicate/overlapping `case` values and multiple `default:` labels in one switch are compile errors |
| `break`, `continue`, `return` | ✓ | |
| `goto` and labels | ✓ | |
| Function declarations and definitions | ✓ | |
| Recursive functions | ✓ | |
| Function pointers — declaration, call, assignment | ✓ | |
| `extern` linkage | ✓ | Every redeclaration of the same global variable within a translation unit canonicalizes onto a single object, and offsets are shared across translation units at link time (#957) -- an `extern` declared, referenced, but never anywhere-defined global is a hard `undefined global: <name>` compile error, mirroring the existing `undefined function: <name>` check; suppressed (not deferred) under `-c`/`--link`, since there is no name-based data relocation mechanism for globals |
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
| `#include`, `#define`, `#undef` | ✓ | `#include` also accepts URLs (`#include <https://...>`) in curl-enabled builds (optional, `CCCC_HAS_CURL=1`), fetched into a cache (`--url-cache-dir`) |
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
| Variable-length arrays (VLA) | ✓ | Allocated via VM heap (block scope only; a variably modified type at file scope is a compile error, matching C11 6.7.6.2p4/6.9.2p3). A VLA of any dimension — including a multi-dimensional VLA (`int v[n][m]; v[1][2]=...;`) — round-trips through `-m`/`-c=native` as a real C VLA (#964/#971). `&v` has the standard (non-decayed) type `int (*)[n]` — like a fixed-size array's `&a`, it does *not* decay to a pointer to the element type, so `&v + 1` strides a whole row, not one element — and yields the array's real data address (#973); a whole-row assignment to a VLA lvalue (`v[1] = w[2];` where each side is itself a row of a multi-dimensional VLA) is a compile error, "not an lvalue", the same as for a fixed-size array (#974). Pointer-to-VLA-row subtraction (`&v[1] - &v[0]` on a 2-D VLA, both sides `int (*)[m]`) divides by the row's runtime byte size, giving the correct element count in both directions (#976). **Brace initialization is a deliberate CCCC extension** — real GCC/clang reject `int v[n] = {...}` outright ("variable-sized object may not be initialized") — supported for any dimension, including a multi-dimensional VLA (`int v[n][m] = {{1,2},{3,4}}`, #977); a short row or short outer initializer zero-fills the remainder, the same as a fixed-size array's partial initializer — an explicit zero-fill (`ND_MEMZERO`, the runtime `vla_size` byte count) now runs ahead of the initializer, matching every other partial-initializer path; a fresh alloca'd block happening to already read as zero is not relied on, so this holds under `--memory-poisoning`/`-2`/`-3` too (#982). Excess initializers (`int u[k] = {1,2,3}` with `k == 1`) are not statically diagnosable in principle — if the length were an integer constant expression it would not be a VLA — but are caught by the same generic runtime bounds machinery as any other overrun, at `-2`/`-3`; silent at default/`-1`, consistent with every other out-of-bounds write at those levels. This is the one deliberate carve-out from `-Wexcess-init` (#1222), which diagnoses the identical excess-element shape for every *statically* checkable target — a fixed-size array, a struct, or a GNU vector. Real GCC/clang reject the VLA construct outright and the extension therefore remains CCCC-only, but with both concrete correctness objections resolved (#1179) it is kept rather than removed. A VLA local (or a pointer-to-VLA local with an initializer) read by a nested function is capturable across the static link, with a fixed multi-dimensional-VLA exception (#1209, see "Nested (GNU) functions" below) |
| Flexible array members (`struct { int n; int arr[]; }`) | ✓ | |
| Designated initialisers — structs and arrays | ✓ | |
| Compound literals | ✓ | Postfix tails bind directly to the literal — `(struct P){30, 12}.x`, `(int[]){1,2,3}[0]`, and `((struct T *){p})->m` all parse (C99 6.5.2p5); `&` of a member through a literal serializes with the & bound inside the literal's initializer chain (#1102) | |
| `inline` functions | ✓ | Dead-function elimination + single-return inlining of `static inline` callees (both unconditional). The VM has no optimiser, so there is no size-thresholded multi-statement inliner |
| `restrict` pointers | ✓ | Parsed and stored on `Type`. The VM has no optimiser, so the non-aliasing property drives no codegen exploitation (the register deref cache and restrict memcpy-loop lowering were removed with the optimiser, #1214). Under `-c=native` the qualifier is serialized verbatim and the host cc acts on it |
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
| `_Generic` type-generic expressions | ✓ | A controlling expression of enumerated type matches an association naming its implementation-defined underlying integer type, and vice versa (`_Generic((enum G){0}, unsigned int: …)` for an all-non-negative `enum G` whose enumerators fit `int`), matching GCC/clang; two separately declared enums stay mutually incompatible. `_Bool` and tagged `struct`/`union` associations match. Inside an association a bare `enum Tag` reference may not restate a C23 `enum E : T` underlying type — the `:` there is the association colon — and doing so is a compile error; a braced `enum Tag : T { … }` *definition* in that position is still accepted (matching GCC; clang rejects any type definition in an association). A selection with two `default` associations, or two associations that specify compatible types (C23 6.7.11p2), is a compile error — except that `long` vs `long long` arms are accepted (CCCC models them as one type) and `char *` vs `const char *` arms are not treated as compatible. Arm *selection* honors pointee qualifiers too, so a `char *` / `const char *` pair resolves by the controlling type rather than by listing order (the `<string.h>` const-correct dispatch macros no longer depend on arm order); a top-level qualifier on an association (`const int:`) can never be selected, since lvalue conversion strips the controlling expression's own `const`/`volatile`/`restrict` first. |
| `_Alignof` | ✓ | |
| `_Alignas` | ✓ | Can request more or less alignment than a declaration's own type, but never less than the type's own alignment — a request below that is a C17 6.7.5p4 constraint violation and a compile error, matching GCC/clang (#1163) |
| `_Static_assert` (and `static_assert` via `<assert.h>`) | ✓ | |
| `_Noreturn` | ✓ | Accepted via keyword, `__attribute__((noreturn))`, and `[[noreturn]]`; emits BTRAP after calls; warns on returns |
| `_Thread_local` | ✓ | TLS segment; each thread receives a private copy from the template |
| `_Atomic` types | ~ | Parser emits `-Wignored-features`; direct access to `_Atomic`-qualified variables uses plain load/store. `<stdatomic.h>` macros (`atomic_load/store/exchange/compare_exchange`) emit ALDR/ASTR/AXCHG/ACAS opcodes with runtime shadow-tracking and mixed-access detection; `atomic_fetch_add/sub/or/xor/and` expand to a real CAS retry loop over the same opcodes (#1184), genuinely atomic on both the VM and `-c=native` — no GIL dependency. `atomic_thread_fence`/`atomic_signal_fence` lower to a real `__atomic_thread_fence`/`__atomic_signal_fence` under `-c=native` (#1188, a new `ND_FENCE` node); under the VM they carry no opcode at all, since the GIL already makes every guest memory access sequentially ordered |
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
| `nullptr` keyword / `nullptr_t` | ✓ | `nullptr_t` is defined in `<stddef.h>` via `typeof(nullptr)`. Serializes under `-c=native`/`-m`/`-c=generated`: cast destinations spell `(void *)` (#1111), since casting *to* `nullptr_t` is not valid C23 syntax; declarations keep the typedef name |
| `_BitInt(N)` arbitrary-precision integers | ✓ | `N` in `[1,65535]` (`BITINT_MAXWIDTH`). `N<=64` uses scalar-register storage with mask/shift truncation; `N>64` uses multi-word (address-based) storage with runtime helper functions for arithmetic, shifts, comparisons, and conversions. `wb`/`uwb` literal suffixes infer their full-precision width directly from the literal's digit text, including widths beyond 64 bits. A file-scope/`static` initializer for a `_BitInt(N)` global (any `N`, including arbitrary constant arithmetic, not just a bare literal) folds at compile time via the same word-array runtime helpers, so the value matches what a local of the same type would compute at runtime. This mask/shift truncation description is the VM's own behaviour; `-c=native`/`-m` reproduces it via an explicit emitted mask whenever `N` isn't already exactly the host container's own width -- see [Serialized-output divergences](NATIVE.md#serialized-output-divergences) for the one-time #1124 gap where it didn't. A bitfield whose declared type is itself a wide `_BitInt` (`T f : W;` where `T`'s width is over 64 bits) is accessed byte-granularly rather than through either the scalar shift/mask path or a whole-container load/store, since the enclosing struct is laid out compactly and need not contain the full `sizeof(T)` bytes (#1125). `_Alignof` for a `_BitInt(N)` container: `N<=64` follows the container's own natural size/alignment (1/2/4/8); `N` in `(64,128]` is 16 (the `__int128` host container `-c=native`/`-m` always lowers it to, see the `__int128` row below and [Serialized-output divergences](NATIVE.md#serialized-output-divergences) for why this deliberately diverges from clang's own native `_BitInt(65..128)` on x86_64); `N>128` stays at 8 (no host container to match, and `sizeof` for such an `N` is not always a multiple of 16) (#1135) |
| Binary integer literals `0b10101010` | ✓ | |
| Digit separators `1'000'000` | ✓ | |
| `[[...]]` attributes | ~ | Parsed; see [ATTRIBUTES.md](ATTRIBUTES.md) for per-attribute status |
| `bool`, `true`, `false` as keywords (not just macros) | ✓ | Real keywords in `--std=c23`/`gnu23`; downgraded to ordinary identifiers below C23. `<stdbool.h>` still works (its macros are gated to pre-C23) |
| `u8` character literals (`u8'x'`) | ✓ | |
| Unnamed function parameters (`void f(int, double)`) | ✓ | |
| `static_assert` without message | ✓ | C23 one-argument form |
| Improved `enum` — underlying type, forward declaration, wide values | ✓ | `enum E : unsigned char { … }` sets size/align/signedness; `enum E : int;` forward-declares; values stored as `int64_t` (C23 §6.7.2.2). A plain enum with no `: type` of its own also *selects* its underlying type from the enumerator values actually seen, matching gcc-16/clang exactly (an extension predating C23 §6.7.2.2's "must represent every enumerator" requirement, [#1175](https://todo.sr.ht/~takeiteasy/cccc/1175)/[#1205](https://todo.sr.ht/~takeiteasy/cccc/1205)): any negative enumerator plus one past `INT32_MAX`/before `INT32_MIN` widens to `long long` (8/8, signed); an all-non-negative enum stays 4 bytes and becomes `unsigned int`, or widens to `unsigned long` (8/8) past `UINT32_MAX`. C17/C23 §6.7.2.2p3 still gives each *enumerator identifier* type `int` (not the enum's own compatible type) whenever the enum has no fixed `: type` base and every value fits `int` — `enum G { G1 = 1 }; sizeof(enum G) == 4` and `(enum G)-1 < 0` is false (unsigned), but `sizeof(G1) == 4` and `-1 < G1` is true (`G1` itself is a plain `int`) — verified against gcc-16/clang, `-std=c17` and default C23 alike |
| Compatible tag redeclarations | ✓ | C23 same-scope compatible `struct`, `union`, and `enum` redeclarations are accepted; incompatible redeclarations are diagnosed |
| Decimal floating-point (`_Decimal32`, etc.) | ✓ | Real IEEE-754-2008 decimal encoding via the Intel BID library, opt-in with `make CCCC_HAS_DECIMAL=1`; declarations/`sizeof`/struct layout always work, decimal literals and arithmetic require the flag. `df`/`dd`/`dl` literal suffixes; `+ - * /`, unary `-`, all six comparisons; conversions to/from integers, binary floating-point, and other decimal widths; `__builtin_decimal_to_chars` for formatting; `DEC32_*`/`DEC64_*`/`DEC128_*` in `<float.h>`. `<decimal_math.h>` (opt-in header) provides the full TS 18661-2 `<math.h>` surface per width (`sqrtd64`, `powd128`, `sind32`, `isnand64`, ...), and `<math.h>`'s `isnan`/`isinf`/`isfinite`/`isnormal`/`signbit`/`fpclassify` dispatch on `_Decimal32/64/128` operands too. `printf`/`scanf` support `%Hf`/`%Df`/`%DDf` with the full `f`/`F`/`e`/`E`/`g`/`G` surface (flags, width, precision), and a decimal value can be passed through the variadic tail of any call (by pointer). `strtod32`/`strtod64`/`strtod128` parse a decimal from a runtime string, exact per IEEE 754-2008. `fesetround()` has real effect on decimal arithmetic (round-to-nearest/down/up/toward-zero) and `fetestexcept()` observes decimal invalid/divide-by-zero/overflow/underflow/inexact. Decimal constant folding in static/global initializers (`static _Decimal64 x = 1.1dd + 2.2dd;`), including a decimal-to-integer or decimal-to-binary-float cast. Deferred: decimal comparisons directly in an integer constant expression, decimal as a fixed FFI parameter or return |
| `char8_t` | ✓ | Defined in `<uchar.h>`; `u8'x'` literals have type `unsigned char` and value `char8_t`; `mbrtoc8`/`c8rtomb` implement the full §7.31.1 incremental UTF-8 state machine |
| Labels before declarations (at block scope) | ✓ | `case`, `default`, and goto-labels may directly precede object declarations; pre-C23 bare declaration after label is a hard error |
| Empty parameter lists `()` — C23 prototype semantics | ✓ | Pre-C23: `()` is an unprototyped (K&R) declaration accepting any arguments; in C23, `()` is equivalent to `(void)`. Use `-Wstrict-prototypes` to warn about non-prototype `()` in pre-C23 modes |
| `exp10`, `sinpi`/`cospi`/`tanpi`, `asinpi`/`acospi`/`atanpi`/`atan2pi` (`<math.h>`) | ✓ | `double`/`float`/`long double` variants implemented (native on macOS/glibc where available, portable shims otherwise, with exact integer/half-integer special-casing for `sinpi`/`cospi`/`tanpi`) |

### Preprocessor

| Feature | Status | Notes |
|---|---|---|
| `#elifdef` / `#elifndef` | ✓ | |
| `#warning` | ✓ | |
| `#embed` | ✓ | Supports `limit()`, `prefix()`, `suffix()`, `if_empty()`, `__has_embed()`; the filename may also be a URL (`#embed <https://...>`) in curl-enabled builds (optional, `CCCC_HAS_CURL=1`), fetched into the same cache URL `#include` uses. `__has_embed()` is URL-aware there too, probing through that same shared cache |
| `__VA_OPT__` | ✓ | |
| `__has_c_attribute` | ✓ | Returns C23 version date (`202311L`) for standard C23 attributes; `1` for CCCC vendor attributes |
| `__has_include` | ✓ | Checks CCCC, `-I`, and `-i` include paths; also accepts URLs in curl-enabled builds, probing the same shared cache a real fetch uses (non-curl builds report 0 for URLs) |
| Leading `#!` (shebang) line | ✓ | CCCC-specific: a `#!` on line 1 of the command-line input file (or a file piped via `-`) is blanked before tokenization, so line numbers are unaffected. Not applied to `#include`d files, which still error on a stray `#!`. |

---

## GNU Extensions

| Feature | Status | Notes |
|---|---|---|
| Statement expressions `({ ... })` | ✓ | |
| 128-bit integers `__int128` / `__int128_t` / `__uint128_t` | ✓ | Implemented on top of the `_BitInt(128)` machinery (multi-word, address-based storage). `unsigned __int128` is honoured; `__SIZEOF_INT128__` is defined so feature-detecting code selects these paths. `sizeof` is 16 and `_Alignof` is 16, matching clang/gcc on every supported target (x86_64 and aarch64, Linux and macOS) (#1135). A file-scope/`static` `__int128` initializer (constant arithmetic, not just a bare literal) folds at compile time the same way as any other wide `_BitInt`. Serializes under `-c=native`/`-m`/`-c=generated` too, as the real host `__int128`/`unsigned __int128` — see [Serialized-output divergences](NATIVE.md#serialized-output-divergences) for `_BitInt(N)` widths beyond 128 bits, which those backends reject |
| `__attribute__((...))` | ~ | Parsed; `aligned`, `packed`, `unused`, `deprecated`, `format`, `nodiscard`, `warn_unused_result`, `fallthrough`, `noreturn`, `error`, `warning`, `constructor`, `destructor`, `sentinel`, `alloc_size`, `malloc` supported (see [ATTRIBUTES.md](ATTRIBUTES.md)) |
| Labels as values `&&label` | ✓ | |
| Computed goto `goto *expr` | ✓ | |
| Switch case ranges `case 1 ... 5:` | ✓ | A range overlapping another case label (range or scalar) is a compile error, same as a duplicate scalar case value |
| Zero-length arrays `int arr[0]` | ✓ | |
| Empty structs and unions | ✓ | GNU extension; empty aggregates have size 0 |
| Nested functions | ✓ | Access to parent-scope variables via static link. Serializes under `-c=native`/`-m`/`-c=generated` too (#1074) — see [Serialized-output divergences](NATIVE.md#serialized-output-divergences) for the lowering shape and its residual gaps |
| Blocks `^{ ... }` (Clang/Apple) | ✓ | Capture-by-value plus `__block` by-reference; nest to arbitrary depth (transitive capture through enclosing descriptors); `Block_copy` heap-duplicates the descriptor so a block can escape its frame, `Block_release` frees that copy. Serializes under `-c=native`/`-m`/`-c=generated` too (#965) — see [Serialized-output divergences](NATIVE.md#serialized-output-divergences) for the lowering shape and its residual gaps |
| `__builtin_*` | ✓ | Lowered by the compiler; see [Built-in Functions](STDLIB.md#built-in-functions) |
| `__thread` storage class | ✓ | TLS segment; per-thread private storage |
| `__restrict` / `__restrict__` | ✓ | Spelling aliases for `restrict` (see `restrict` entry above) |
| `__inline` / `__inline__` | ✓ | Spelling aliases for `inline`; recognized as GCC keyword aliases (GCC compatibility) |
| `__typeof__` | ✓ | Synonym for `typeof` |
| `asm(...)`/`__asm__(...)`/`__asm(...)` inline assembly | ✓ | `asm(...)` statements are no-ops by default; `--asm-passthru` compiles via native CC and executes via FFI; custom callback via `cc_set_asm_callback`. All three alternate-keyword spellings are accepted in statement position, matching `is_asm_label_tok`'s existing acceptance of all three for `asm("symbol")` declarator labels (#1130). **`-c=native`/`-m`/`-c=generated` always emit `__asm__(...)` verbatim** (never bare `asm`, which GCC/clang both disable as a keyword under a strict ISO `-std=cNN` — confirmed on real GCC and on the `cccc-linux-amd64` container's clang, which reproduce the failure Apple clang's leniency does not), regardless of `--asm-passthru` — the one construct where serialized output deliberately does not mirror default VM behaviour, since there is no way to evaluate host assembly inside the VM at all. Executing it is the host compiler's job, and `--asm-passthru` governs VM execution only. See [Serialized-output divergences](NATIVE.md#serialized-output-divergences) and the #1119 note there (dedicated asm suite, skipped natively) |
| GNU `asm("symbol")` declaration labels | ~ | Supported on function declarations, including typedef-based and multi-declarator declarations; the label is used as the external FFI symbol name |
| `__attribute__((vector_size(N)))` generic vectors | ~ | 128-, 256-, and 512-bit vectors (16/32/64-byte total size) — e.g. `v4f32`/`v8f32`/`v16f32`, `v2f64`/`v4f64`/`v8f64`, `v4i32`/`v8i32`/`v16i32`, and the corresponding `i64`/`i16`/`i8` lane layouts at each width; any other width (non-power-of-two byte counts, or wider than 64 bytes) is rejected with a diagnostic. Element-wise `+ - * /` and unary `-`/`~` on all lane types; integer lanes additionally support `% & \| ^` and integer `/`/`%`, each trapping per-lane on a zero divisor or `MIN/-1` overflow (stricter than default scalar `/`, which does not trap — matches scalar `DIVC`'s policy); `&`/`\|`/`^`/`~` are rejected on float lanes. Comparisons `== != < <= > >=` produce a per-lane all-ones(`-1`)/all-zero mask in a same-width **signed** integer vector (GCC semantics); ordered `< <=` on unsigned-int lanes compare the unsigned view. GNU vector `?:` select is supported (nonzero-per-lane condition, matching lane count/width). Scalar operands broadcast through arithmetic (`v + 5.0f`), matching GCC/clang — a bare scalar cannot initialize or be assigned to a whole vector. `v[i]` subscript supports a runtime-variable index. Brace-initializer syntax (`v4sf a = {1,2,3,4};`, including partial and nested forms) is supported, as are compound literals (`(v4sf){1,2,3,4}`) used as expressions, with a `static` storage-class specifier, or as the entire initializer of another global/static variable (`v4sf g = (v4sf){1,2,3,4};`). Designated lane initializers (`{[2]=3.0f}`) are rejected with a diagnostic, matching GCC/clang — vector types are non-aggregate in their model, and designated initializers only apply to aggregates. `__builtin_convertvector(expr, type)` converts between vectors with the same lane count; only `int32<->float32` and `int64<->float64` lane pairs are representable (matching lane counts forces matching element byte sizes), so same-domain conversions (e.g. changing signedness/width without crossing int/float) are rejected. `__builtin_shuffle` supports both a **compile-time-constant, brace-enclosed** index mask (`__builtin_shuffle(v, {3,2,1,0})`, 1- or 2-vector form — CCCC's own constant-mask form, closer to clang's `__builtin_shufflevector` than upstream GCC's syntax) and a **runtime or named integer vector mask** (`__builtin_shuffle(v, mask)` / `__builtin_shuffle(v1, v2, mask)`, matching upstream GCC's general form) — the mask must be an integer vector with the same lane count and element byte width as the vector being shuffled. Both forms lower via the same vector-subscript machinery with no dedicated opcode; the constant form range-checks each index at compile time (an out-of-range index is a compile error), while the runtime form takes each index modulo the lane count (1-vector) or twice the lane count (2-vector), matching GCC's documented wraparound semantics — a runtime index can't be rejected at compile time. Vectors can be passed as function parameters and returned by value: passed by memory (a caller-side scratch copy sized to the argument's own width, address handed to the callee, like a struct-by-value arg), returned via the RETBUF rotating pool (like a struct-by-value return). This includes a variadic `...` parameter: the by-memory scratch-pointer convention means a variadic vector arg always occupies exactly one 8-byte slot regardless of the vector's width, and `<stdarg.h>`'s `va_arg` detects a vector `type` via `__builtin_classify_type` and dereferences the slot instead of reading it directly — matches gcc/clang, which also accept a vector through `...` (verified). Not supported through the native FFI marshalling path (extern/`dlopen`ed functions — libffi has no portable vector type, and there's no struct-by-value FFI path to build on either) or a GNU/Apple block invocation — each rejected with a diagnostic. |

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
| `#pragma comment(lib, "x")` | ✗ | Written in guest source it is silently ignored (`handle_pragma_body`, `src/preprocess.c`, has no `comment` branch) — it does not queue a library the way `#pragma cccc link("x")` does. Not emitted either: `-E`/`-m`/`-c=generated` output re-emits a queued `#pragma cccc link("x")` library as `#pragma cccc link("x")` itself (`src/serialize_program.c`/`src/tokenize.c`), not as `comment(lib, ...)`, so that output round-trips if fed back to cccc (#1149, resolved). `-c=native` doesn't emit either spelling — the library reaches the host linker directly via `-l` |
| `#pragma warning(push/pop/disable/default)` / `suppress:` | ✗ | Maps to CCCC's `-W` system — pending |
| `#pragma pack(N)` / `pack()` / `pack(push[, ident][, N])` / `pack(pop[, ident])` | ✓ | Caps (rather than forces, like `packed` does) a subsequent struct/union's implicit member alignment at `N` (a power of two, 1–16) — an explicit member `aligned(N)`/`_Alignas(N)` request is capped too, unlike `__attribute__((packed))`, which an explicit request always overrides (#1163; verified directly against gcc-16/clang). Parsed into a push/pop stack (`handle_pragma_pack`, `src/preprocess.c`, mirroring `#pragma GCC diagnostic push/pop`'s own mechanism) and stamped onto every token so `struct_decl`/`union_decl` (`src/parse_types.c`) can read the value in effect at each aggregate's definition, regardless of source order. Re-emitted for `-c=native`/`-m`/`-c=generated` as `#pragma pack(push, N)`/`pack(pop)` wrapped tightly around just the affected definition (`src/serialize_type.c`) — not the pre-#1173 verbatim, hoisted-to-the-top replay, which applied to the wrong structs entirely. A `#pragma pack` change *inside* a struct/union body (between two members) is rejected outright rather than silently mis-applied. An anonymous (tagless, no typedef) struct/union under an active pack — which has no file-scope definition line for a `#pragma pack(push, N)` to wrap — is also rejected: give it a tag or typedef. (#1173) |
| `#pragma message("...")` | ✓ | |
| `#pragma region` / `#pragma endregion` | ✗ | IDE-only, no-op — pending |
| `#pragma intrinsic(...)` / `#pragma function(...)` | ✗ | Intrinsic toggle, no-op — pending |
| `#pragma optimize(...)` | ✗ | No-op — the VM has no optimiser |
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

Attribute support (`__attribute__((...))`, `[[...]]`, and the `@name`
shorthand) has its own reference: [ATTRIBUTES.md](ATTRIBUTES.md).

## See also

- [NATIVE.md](NATIVE.md) — `-c=native`/`-m`/`-c=generated` serializer scope:
  the admissibility rule, GNU-extension and C23-lowering coverage, refused
  constructs, and the documented VM/serialized-output divergences.
- [STDLIB.md](STDLIB.md) — standard library, POSIX headers, and
  `__builtin_*` coverage.

# Attributes

GNU `__attribute__`, C23 `[[...]]` and the `@name` shorthand, with partial semantic support.

CCCC supports GNU `__attribute__((...))` and C23 `[[...]]` attribute syntaxes.
The most common diagnostic and layout attributes are fully implemented; the
rest are **parsed and silently ignored** by the attribute consumer.

## Feature-Test Preprocessor Operators

CCCC provides the common `__has_*` operators in preprocessor conditionals:
`__has_include`, `__has_include_next`, `__has_feature`, `__has_extension`,
`__has_attribute`, `__has_builtin`, `__has_c_attribute`,
`__has_cpp_attribute`, `__has_declspec_attribute`, and `__has_warning`.

Each operator is visible to `defined`, `#ifdef` and `#ifndef`, and also
evaluates outside `#if`:

```c
#ifdef __has_builtin
int has_expect = __has_builtin(__builtin_expect); // 1
#endif
```

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
[Parsed but Ignored](attributes-ignored.md#parsed-but-ignored)) but are recognized and consumed by
name, matching real GCC/Clang so headers that feature-test on them don't
diverge; genuinely unrecognized attributes return `0`.
`__has_c_attribute` returns the C23 version date (`202311L`) for standard C23
attributes (C23 N3220 §6.10.10.2), `1` for CCCC vendor attributes (including the
[`<cccc/derive.h>`](derive.md) ones such as `cccc::accessors`, whether
or not the header is included), and `1`
for any attribute `__has_attribute` recognizes when it is written in the `gnu`
or `__gnu__` scope (`__has_c_attribute(gnu::cold)`); unsupported or unknown
attributes return `0`. `__has_cpp_attribute` returns `0`.

`__has_declspec_attribute` and `__has_warning` are recognized (so headers
using them parse instead of erroring) but always return `0` — CCCC supports
no `__declspec` attributes and has no `-W`-name-keyed diagnostic registry.
`__has_include_next(<f.h>)` is `1` when a file named `f.h` exists in a search
directory after the one the current file was found in, which is what
`#include_next <f.h>` would open. In a file that was not found through the
search paths, such as the main source file, it searches every directory.

Any other `__has_*`-shaped operator CCCC doesn't recognize evaluates to `0`
and emits a `-Wcpp` warning naming it, rather than the confusing "not a
function" error a raw, unhandled `__has_foo(...)` would otherwise produce.

## Quick Reference

| Attribute | Syntax | Status | Semantics |
|-----------|--------|--------|-----------|
| `aligned(N)` | GNU | ✓ | Sets minimum alignment on types, variables, and struct/union members (also `[[gnu::aligned(N)]]`) |
| `packed` | GNU | ✓ | Suppresses *implicit* struct member padding — an explicit member `aligned(N)`/`_Alignas(N)` still applies (#1163) (also `[[gnu::packed]]`). See `#pragma pack(N)` (pragma table above) for the related but distinct directive that CAPS rather than forces alignment, and which caps an explicit member request too (#1173) |
| `unused` / `__unused__` | GNU | ✓ | Suppresses `-Wunused` warnings |
| `deprecated` / `__deprecated__` | GNU | ✓ | Emits `-Wdeprecated` warnings |
| `deprecated("msg")` | GNU | ✓ | Emits `-Wdeprecated` with custom message |
| `maybe_unused` | C23 | ✓ | Suppresses `-Wunused` warnings |
| `deprecated` | C23 | ✓ | Emits `-Wdeprecated` warnings |
| `deprecated("msg")` | C23 | ✓ | Emits `-Wdeprecated` with custom message |
| `macro` | GNU | ✓ | CCCC-specific; compile-time macro (see [macros.md](macros.md)) |
| `comptime` | GNU | ✓ | CCCC-specific; compile-time variable evaluation (see [macros.md](macros.md)) |
| `format(printf/scanf, …)` | GNU | ✓ | Type-check printf/scanf format strings at compile time, including length-modifier-aware argument type validation (`%ld` → `long`, `%zu` → `unsigned long`, `%Lf` → `long double`); gated by `--format-string-checks`. On an *integer* conversion (`%Ld`/`%Li`/`%Lu`/`%Lo`/`%Lx`) the `L` modifier is accepted as the GNU pre-C99 spelling of `ll` and checked as `long`/`unsigned long`, matching glibc and gcc/clang `-Wformat` — the runtime formatter treats it the same way, in both `printf` and `scanf` directions. The `%n` conversion is length-modifier-aware too (`%hhn` → `char *`, `%hn` → `short *`, `%ln`/`%lln`/`%Ln`/`%jn`/`%zn`/`%tn` → an 8-byte integer pointer of *either* signedness — `%n` has no signed/unsigned split, so `long *` and `unsigned long *` are interchangeable, matching gcc/clang; bare `%n` stays lenient); the runtime `%n` store follows the modifier's width on both sides — `printf` writes exactly a `signed char`/`short`/`long`/`long long`/`intmax_t`/`size_t`/`ptrdiff_t`, matching glibc and Apple libc. A `long double` passed to a plain `%f`/`%e`/`%g` (no `L`) is now a width mismatch, not a promotion — the runtime marshals a variadic `long double` as a real `long double` (see the `<float.h>` note and design note below), so the argument frame would desync, matching gcc/clang `-Wformat`. The `%L` conversions themselves are performed at `double` precision, like all `long double` arithmetic. Only `printf` (plus `gnu_printf`, `printf0`) and `scanf` (plus `gnu_scanf`) are checked, each also with `__x__` decoration. `strftime`, `strfmon`, `os_log`, `os_trace`, `NSString`, `CFString`, `kprintf`, `syslog` and the `ms_*` archetypes are accepted without validation. Any other archetype gives a `-Wattributes` warning and is likewise unchecked |
| `nodiscard` | C23 | ✓ | Warns on discarded return values (`-Wnodiscard`, part of `-Wall`) |
| `fallthrough` | C23 | ✓ | Suppresses fallthrough warning in switch cases (`-Wfallthrough`, part of `-Wextra`) |
| `noreturn` | C23 / GNU | ✓ | Emits `BTRAP` after calls; warns on returns |
| `optimize("ON")` / `optimize(N)` | GNU / CCCC | ~ | Parsed, syntax-checked, then ignored with a warning (the VM has no optimiser) |
| `cleanup(fn)` | GNU | ✓ | Scope-exit callback: calls `fn(&var)` when the variable goes out of scope |
| `error("msg")` | GNU | ✓ | Emits a compile-time error when called; DCE-aware: suppressed inside statically-dead positions (constant-fold + unsigned boundary tautology): `if`/`else` branches, `while(0)`/`for(;0;)` bodies and increment expressions, `false && call()` / `true \|\| call()` short-circuit operands, ternary `cond ? dead : live` branches, GNU elvis `truthy ?: dead` — enabling the `_FORTIFY_SOURCE` `__chk_fail` idiom |
| `warning("msg")` | GNU | ✓ | Emits a compile-time warning when called; same DCE-aware suppression as `error` |
| `warn_unused_result` / `__warn_unused_result__` | GNU | ✓ | GNU equivalent of `[[nodiscard]]`: warns if the return value is discarded (`-Wnodiscard`, part of `-Wall`) |
| `nonnull` / `nonnull(N,...)` | GNU / C23 | ✓ | Warns when a statically- or flow-provably-null argument is passed to a nonnull-marked parameter (`-Wnonnull`, part of `-Wall`); a merely maybe-null argument warns under the opt-in `-Wmaybe-nonnull` |
| `returns_nonnull` | GNU / C23 | ✓ | Warns when a statically- or flow-provably-null value is returned from a `returns_nonnull` function (`-Wnonnull`, part of `-Wall`); a merely maybe-null return warns under the opt-in `-Wmaybe-nonnull` |
| `constructor` / `constructor(N)` | GNU (C23: `[[gnu::constructor]]`) | ✓ | Runs `void(void)` function before `main()`, ordered by priority (lower first; unprioritised functions run last) |
| `destructor` / `destructor(N)` | GNU (C23: `[[gnu::destructor]]`) | ✓ | Runs `void(void)` function after `main()` returns normally or an explicit `exit()` call, in reverse priority order (higher first; unprioritised functions run first) |
| `sentinel` / `sentinel(N)` | GNU (C23: `[[gnu::sentinel]]`) | ✓ | Warns at each call site when the expected trailing variadic argument is not a literal, pointer-typed `NULL` (`-Wsentinel`, part of `-Wall`); static syntactic check only, no runtime enforcement. Applying it to a non-variadic function warns under `-Wattributes` instead |
| `alloc_size(n)` / `alloc_size(n,m)` | GNU (C23: `[[gnu::alloc_size]]`) | ✓ | Marks a function as an allocator whose return value has a compile-time-computable byte size: argument `n` (1-based) for the single-index form, or the product of arguments `n` and `m` for the two-index (calloc-style) form. Consulted by `__builtin_object_size`/`__builtin_dynamic_object_size` heap-allocation sizing (see below) — the sole recognition mechanism, superseding earlier hardcoded name matching |
| `malloc` | GNU (C23: `[[gnu::malloc]]`) | ~ | Parsed and stored (self-describes a fresh, non-aliasing allocator, matching libc's `malloc`/`calloc`/`aligned_alloc`) but not yet wired to any aliasing optimization or nonnull inference — informational only; the aliasing optimizations GCC uses it for need a memory-dependency pass the VM optimizer doesn't have yet (see `__attribute__((malloc))` below) |
| `single` / `array` / `ntarray` | CCCC (post-`*` position only) | ✓ | Checked C-style checked-pointer kind (#770/#482); see [Checked Pointers](safety-checked-pointers.md#checked-pointers) |
| `count(n)` / `byte_count(n)` / `bounds(lo,hi)` / `bounds(unknown)` | CCCC (post-`*` position only) | ✓ | Checked-pointer bounds declaration (#770/#483); enforced at runtime under `--checked-pointers` (see [safety.md](safety-checked-pointers.md#checked-pointers)) |
| `assume` / `dynamic` | CCCC (post-`*` position, cast type-name only) | ✓ | Converts an unchecked pointer into a checked one: `assume` trusts the claim, `dynamic` verifies it against the source's own bounds; see [Checked Pointers § Bounds casts](safety-checked-pointers.md#checked-pointers) |
| `checked` / `unchecked` | CCCC (function definition or compound statement) | ✓ | Opens a checked/unchecked region: within it, an unchecked pointer declaration or an unsafe pointer cast is a compile error, always on regardless of `--checked-pointers`; see [Checked Regions](safety-checked-regions.md#checked-regions) |
| `kernel` | CCCC (function) | ✓ | Rejects the function, and everything it calls, if it uses something a Metal or CUDA kernel cannot run; see [GPU Kernel Subset](attributes-kernel.md) |
| `global` / `local` / `constant` / `private` / `generic` | CCCC (declaration specifiers) | ✓ | Marks the address space a pointer refers to, or an object lives in, for `[[cccc::kernel]]` code; see [Address Spaces](attributes-kernel.md#address-spaces) |
| `vector_size(N)` | GNU | ✓ | Makes the declared scalar type an `N`-byte vector (16, 32 or 64); see [Vector Size](attributes-kernel.md#vector-size) |
| `cccc_visible_lanes(N)` | GNU (vector typedef) | ✓ | Lets a `vector_size` vector expose fewer lanes than it stores, for OpenCL's `float3`; see [Visible Lanes](attributes-kernel.md#visible-lanes) |
| *all others* | Both | ~ | Parsed and silently ignored — see [Parsed but Ignored](attributes-ignored.md#parsed-but-ignored) |

`__has_attribute` returns `1` for `error`, `warning`, `warn_unused_result`, and
`__warn_unused_result__` — all four carry real compile-time semantics (see
above), matching real GCC/Clang.

## Supported Attributes

### `__attribute__((aligned(N)))`

Sets minimum alignment for a type, variable, or struct/union member. The argument is a constant expression specifying the alignment in bytes. Can also be used without an argument (`__attribute__((aligned))`) to request maximum useful alignment (16 on every target this project supports). Honored in every position GCC/clang accept it: on the struct/union body itself, on a global or local variable, and — declspec-prefix or declarator-suffix — on an individual struct/union member (`int b __attribute__((aligned(16)));`, #1160), *including* a bit-field member (`int b : 5 __attribute__((aligned(16)));`, declspec-prefix or declarator-suffix, #1165) — it aligns the bit-field's own storage unit and always raises the aggregate's alignment, even on an unnamed bit-field (see the Bitfields row above for the one GCC/clang divergence this has). It only ever *raises* alignment relative to a member's own natural alignment (only `packed` lowers it); `_Alignas(N)`, by contrast, is a genuine assignment rather than a floor, and can raise a type's own natural alignment the same way `aligned(N)` does (e.g. `_Alignas(16) int b`) — but naming an `N` *smaller* than the declared type's own alignment (e.g. `_Alignas(1) long b`) is a C17 6.7.5p4 constraint violation and a compile error, matching GCC/clang exactly (`'_Alignas' specifiers cannot reduce alignment`, #1163); on a bit-field specifically, `_Alignas(N)`/`alignas(N)` is rejected outright regardless of `N` (`alignment specified for bit-field`, matching GCC, #1165) — only the GNU `aligned(N)` spelling is legal there. On a `packed` struct or union, an explicit member `aligned(N)`/`_Alignas(N)` still applies and still widens that member's offset and the aggregate's own alignment — `packed` only suppresses a member's *implicit* (natural) alignment, matching GCC/clang exactly (#1163); a `packed` union's own alignment (unlike a struct's) previously ignored `packed` altogether and is now also suppressed to 1 absent an explicit member request. The C23 spelling `[[gnu::aligned(N)]]` is equivalent in every position except directly after a struct/union definition's closing `}`, where GCC itself silently ignores it (unlike the GNU `__attribute__((aligned(N)))` spelling in that same trailing position, which GCC does honor — CCCC matches GCC in both spellings), and except directly after a bit-field's width, where GCC/clang reject any `[[...]]` attribute but CCCC accepts it (`unsigned b : 3 [[gnu::aligned(4)]];`, the form `@name` rewrites to) alongside `__attribute__((...))`, in either order. The variable's real address honours the alignment for globals, thread-locals and stack locals alike (including a local's by-value copy as a function argument), up to 64 bytes; a larger `N` is accepted but placed at 64.

A type-level `aligned(N)` (on the struct/union itself) and a member's own explicit alignment (`_Alignas(N)` or `aligned(N)`, including on a bit-field) both survive `-c=native`/`-m`/`-c=generated` (#1129/#1160/#1165) — re-emitted only when they actually widen the layout beyond what the members alone would produce, so ordinary structs carry no extra attribute noise. A bit-field's own alignment is always re-emitted as a trailing GNU attribute after its width, never as an `_Alignas(N)` prefix (which GCC rejects on a bit-field). An object's own `_Alignas(N)` (`Obj.align`) was already covered separately (#1136).

`__alignof__(v)` / `_Alignof(v)` on a variable or struct member names its declared alignment (`__alignof__(cache_line) == 64` below); any other expression reports its type's alignment, including a typedef's `aligned(N)` (`typedef int i64 __attribute__((aligned(64)));` gives `__alignof__(*p) == 64` for `i64 *p`). An operation whose result is a different type reports that type: `__alignof__(g + 1) == 4`, and `__alignof__(g + g) == 64`.

```c
struct __attribute__((aligned(16))) vec4 { float x, y, z, w; };
int __attribute__((aligned(64))) cache_line;

struct Header {
    char a;
    int  b __attribute__((aligned(16))); // offsetof(Header, b) == 16
};
```

### `__attribute__((packed))`

Prevents the compiler from inserting padding between struct/union members, and can also prevent alignment-based padding at the end of a struct. Only *implicit* (natural) member alignment is suppressed — an explicit `aligned(N)`/`_Alignas(N)` on a member's own declarator still applies and still widens that member's offset and the aggregate's own alignment, matching GCC/clang exactly (#1163); a member whose *type* is independently `aligned(N)` does not count as explicit and stays suppressed. A packed union's own alignment is suppressed the same way a packed struct's is (also #1163 — previously `union_decl()` had no `packed` check at all, so a packed union kept its natural alignment). Also accepts the C23 spelling `[[gnu::packed]]`, in the same struct/union-body position — but, like `[[gnu::aligned(N)]]` above, not directly after the closing `}` of a definition, where GCC ignores it (and warns). Survives `-c=native`/`-m`/`-c=generated` (#1129) — previously dropped silently, producing a native binary whose struct layout diverged from the VM's.

`#pragma pack(N)` (pragma table above) is the related but semantically distinct directive: it CAPS a subsequent aggregate's alignment at `N` rather than forcing it to 1, and — unlike `packed` — the cap applies even to an explicit member `aligned(N)`/`_Alignas(N)` request, confirmed directly against gcc-16/clang (#1173).

```c
struct __attribute__((packed)) {
    char c;
    int i;  // directly follows c with no padding
};
```

### `__attribute__((unused))` / `__attribute__((__unused__))` / `[[maybe_unused]]`

Suppresses `-Wunused` warnings on variables, functions, parameters, typedefs, and labels. Both the GNU and C23 forms (`[[maybe_unused]]`) are recognised with full semantic effect.

```c
int __attribute__((unused)) x;       // GNU
int [[maybe_unused]] y;              // C23
__attribute__((unused)) static void helper(void) {}
```

### `__attribute__((deprecated))` / `__attribute__((__deprecated__))` / `[[deprecated]]`

Marks a declaration as deprecated. Warnings are emitted via `-Wdeprecated` when the identifier is used. Supports an optional message string that is included in the warning output.

```c
int __attribute__((deprecated("use bar instead"))) old_func(void);
int [[deprecated]] legacy_var;
```

## Guide pages

| Page | Covers |
|---|---|
| [attributes-cccc.md](attributes-cccc.md) | CCCC-specific `comptime`, `emit` and include forms |
| [attributes-gnu.md](attributes-gnu.md) | `pure`, `const`, `optimize`, `sentinel`, `alloc_size`, `malloc`, `designated_init` |
| [attributes-cleanup.md](attributes-cleanup.md) | `cleanup`, `constructor` and `destructor` |
| [attributes-nonnull.md](attributes-nonnull.md) | `nonnull` and `returns_nonnull` |
| [attributes-bounds.md](attributes-bounds.md) | `single`, `array`, `ntarray`, `count`, `bounds`, `assume`, `checked` |
| [attributes-kernel.md](attributes-kernel.md) | The GPU kernel attribute subset |
| [attributes-syntax.md](attributes-syntax.md) | `@` shorthand, grammar position, scope and `asm` labels |
| [attributes-ignored.md](attributes-ignored.md) | Attributes that are parsed and ignored |

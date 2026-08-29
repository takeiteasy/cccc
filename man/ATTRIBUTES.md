# Attribute Support

CCCC supports GNU `__attribute__((...))` and C23 `[[...]]` attribute syntaxes.
The most common diagnostic and layout attributes are fully implemented; the
rest are **parsed and silently ignored** by the attribute consumer.

## Feature-Test Preprocessor Operators

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
| `macro` | GNU | ✓ | CCCC-specific; compile-time macro (see [MACROS.md](MACROS.md)) |
| `comptime` | GNU | ✓ | CCCC-specific; compile-time variable evaluation (see [MACROS.md](MACROS.md)) |
| `format(printf/scanf, …)` | GNU | ✓ | Type-check printf/scanf format strings at compile time, including length-modifier-aware argument type validation (`%ld` → `long`, `%zu` → `unsigned long`, `%Lf` → `long double`); gated by `-F`. On an *integer* conversion (`%Ld`/`%Li`/`%Lu`/`%Lo`/`%Lx`) the `L` modifier is accepted as the GNU pre-C99 spelling of `ll` and checked as `long`/`unsigned long`, matching glibc and gcc/clang `-Wformat` — the runtime formatter treats it the same way, in both `printf` and `scanf` directions. The `%n` conversion is length-modifier-aware too (`%hhn` → `char *`, `%hn` → `short *`, `%ln`/`%lln`/`%Ln`/`%jn`/`%zn`/`%tn` → an 8-byte integer pointer of *either* signedness — `%n` has no signed/unsigned split, so `long *` and `unsigned long *` are interchangeable, matching gcc/clang; bare `%n` stays lenient); the runtime `%n` store follows the modifier's width on both sides — `printf` writes exactly a `signed char`/`short`/`long`/`long long`/`intmax_t`/`size_t`/`ptrdiff_t`, matching glibc and Apple libc. A `long double` passed to a plain `%f`/`%e`/`%g` (no `L`) is now a width mismatch, not a promotion — the runtime marshals a variadic `long double` as a real `long double` (see the `<float.h>` note and design note below), so the argument frame would desync, matching gcc/clang `-Wformat`. The `%L` conversions themselves are performed at `double` precision, like all `long double` arithmetic. also accepts GNU/Clang alternate spellings `__printf__`, `gnu_printf`, `printf0`, `__printf0__`, `__scanf__`, `gnu_scanf`, `strftime`, `__strftime__`, `os_log`, `__os_log__` (latter two parsed without validation) |
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
| `single` / `array` / `ntarray` | CCCC (post-`*` position only) | ✓ | Checked C-style checked-pointer kind (#770/#482); see [Checked Pointers](SAFETY.md#checked-pointers) |
| `count(n)` / `byte_count(n)` / `bounds(lo,hi)` / `bounds(unknown)` | CCCC (post-`*` position only) | ✓ | Checked-pointer bounds declaration (#770/#483); enforced at runtime under `--checked-pointers` (see [SAFETY.md](SAFETY.md#checked-pointers)) |
| *all others* | Both | ~ | Parsed and silently ignored — see [Parsed but Ignored](#parsed-but-ignored) |

`__has_attribute` returns `1` for `error`, `warning`, `warn_unused_result`, and
`__warn_unused_result__` — all four carry real compile-time semantics (see
above), matching real GCC/Clang.

## Supported Attributes

### `__attribute__((aligned(N)))`

Sets minimum alignment for a type, variable, or struct/union member. The argument is a constant expression specifying the alignment in bytes. Can also be used without an argument (`__attribute__((aligned))`) to request maximum useful alignment (16 on every target this project supports). Honored in every position GCC/clang accept it: on the struct/union body itself, on a global or local variable, and — declspec-prefix or declarator-suffix — on an individual struct/union member (`int b __attribute__((aligned(16)));`, #1160), *including* a bit-field member (`int b : 5 __attribute__((aligned(16)));`, declspec-prefix or declarator-suffix, #1165) — it aligns the bit-field's own storage unit and always raises the aggregate's alignment, even on an unnamed bit-field (see the Bitfields row above for the one GCC/clang divergence this has). It only ever *raises* alignment relative to a member's own natural alignment (only `packed` lowers it); `_Alignas(N)`, by contrast, is a genuine assignment rather than a floor, and can raise a type's own natural alignment the same way `aligned(N)` does (e.g. `_Alignas(16) int b`) — but naming an `N` *smaller* than the declared type's own alignment (e.g. `_Alignas(1) long b`) is a C17 6.7.5p4 constraint violation and a compile error, matching GCC/clang exactly (`'_Alignas' specifiers cannot reduce alignment`, #1163); on a bit-field specifically, `_Alignas(N)`/`alignas(N)` is rejected outright regardless of `N` (`alignment specified for bit-field`, matching GCC, #1165) — only the GNU `aligned(N)` spelling is legal there. On a `packed` struct or union, an explicit member `aligned(N)`/`_Alignas(N)` still applies and still widens that member's offset and the aggregate's own alignment — `packed` only suppresses a member's *implicit* (natural) alignment, matching GCC/clang exactly (#1163); a `packed` union's own alignment (unlike a struct's) previously ignored `packed` altogether and is now also suppressed to 1 absent an explicit member request. The C23 spelling `[[gnu::aligned(N)]]` is equivalent in every position except directly after a struct/union definition's closing `}`, where GCC itself silently ignores it (unlike the GNU `__attribute__((aligned(N)))` spelling in that same trailing position, which GCC does honor — CCCC matches GCC in both spellings), and except directly after a bit-field's width, where no C23 attribute at all is accepted by GCC/clang or CCCC (CCCC's own constant-expression parser reaches the same syntax error GCC's does, rather than a semantic rejection).

A type-level `aligned(N)` (on the struct/union itself) and a member's own explicit alignment (`_Alignas(N)` or `aligned(N)`, including on a bit-field) both survive `-c=native`/`-m`/`-c=generated` (#1129/#1160/#1165) — re-emitted only when they actually widen the layout beyond what the members alone would produce, so ordinary structs carry no extra attribute noise. A bit-field's own alignment is always re-emitted as a trailing GNU attribute after its width, never as an `_Alignas(N)` prefix (which GCC rejects on a bit-field). An object's own `_Alignas(N)` (`Obj.align`) was already covered separately (#1136).

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

### `__attribute__((comptime))` / `[[cccc::comptime]]` (CCCC-specific)

These are CCCC's own extensions for compile-time metaprogramming. They are intercepted by the preprocessor and do not reach the general attribute parser. See [MACROS.md](MACROS.md) for details.

```c
[[cccc::comptime]] int square(int x) { return x * x; }
__attribute__((comptime)) const int version = 42;
__comptime int helper(void) { return 42; }
```

### `#include [[cccc::comptime]]` / `#include @comptime` (CCCC-specific)

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

### Preprocessor `[[cccc::emit]]` / `@emit` / `__attribute__((emit))` (CCCC-specific)

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

### Preprocessor `[[cccc::comptime]]` / `@comptime` / `__attribute__((comptime))` (CCCC-specific)

Routes a preprocessor directive to the comptime compilation stream instead of
runtime source:

```c
#define @comptime CT_VALUE 42
#ifdef @comptime CT_VALUE
#define @comptime CT_SEEN 1
#endif @comptime
```

## Side-Effect Annotations

### `__attribute__((pure))` / `[[gnu::pure]]`

Marks a function as *pure*: it may read global state but has no side effects
and always returns the same result for the same arguments given unchanged
global state.

**Effect:** parsed and stored on the function. The VM has no optimiser, so
this drives no dead-call elimination or CSE — it is informational (visible to
`__has_attribute` and reflection) and forwarded to the host cc under
`-c=native`, which acts on it.

Accepted spellings: `__attribute__((pure))`, `__attribute__((__pure__))`,
`[[gnu::pure]]`, `[[cccc::pure]]`.

**`__has_attribute`:** returns `1` for `pure` and `__pure__`.

---

### `__attribute__((const))` / `[[gnu::const]]`

Marks a function as *const*: it has no side effects and its return value
depends only on its arguments (no global reads).  Stronger than `pure`.

**Effect:** parsed and stored, informational only in VM mode (no optimiser);
forwarded to the host cc under `-c=native`.

Accepted spellings: `__attribute__((const))`, `__attribute__((__const__))`,
`[[gnu::const]]`, `[[cccc::const]]`.

> **Note:** `gnu::const` would collide with the C keyword `const` if written
> as `[[const]]` — the `gnu::` namespace qualifier is required for the C23
> spelling.

**`__has_attribute`:** returns `1` for `const` and `__const__`.

---

### `__attribute__((optimize(...)))` / `[[cccc::optimize(N)]]` / `@optimize(N)`

Parsed, syntax-checked, and then **ignored** with a `-Wignored-features`
warning. The VM has no optimiser, so a per-function optimisation level has
nothing to act on. The attribute is kept in the grammar for compatibility
with GCC/Clang sources that carry it; use `-c=native -O<n>` if a real host
optimisation level is wanted.

Accepted spellings: `__attribute__((optimize("O2")))` (string form `"O0"`
through `"O4"`, optional leading `-`), `[[cccc::optimize(3)]]` (integer `0`
through `4`), and the `@optimize(2)` shorthand. A malformed argument is still
a hard error.

**`__has_attribute` / `__has_c_attribute`:** both return `1` for `optimize`
(as a CCCC vendor attribute).

---

### `__attribute__((cleanup(fn)))` / `[[gnu::cleanup(fn)]]`

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

### `__attribute__((constructor))` / `__attribute__((destructor))` / `[[gnu::constructor]]` / `[[gnu::destructor]]`

Registers a `void(void)` function to run before `main()` (constructor) or
after `main()` returns or an explicit `exit()` call (destructor). An
optional integer priority controls relative ordering among multiple
constructors/destructors:

```c
__attribute__((constructor)) void init(void) { /* runs before main() */ }
__attribute__((constructor(101))) void init_early(void) { /* lower number runs first */ }

__attribute__((destructor)) void fini(void) { /* runs after main() returns or exit() is called */ }
[[gnu::destructor(101)]] void fini_late(void) { /* higher number runs first */ }
```

**Ordering:** constructors run in ascending priority order (lower numbers
first); functions with no explicit priority form the default group and run
last. Destructors run in the reverse order — descending priority (higher
numbers first), with the default group running first. A simple stable sort
is used; CCCC does not replicate full ELF `.init_array` priority-band
semantics.

**Limitations:**

- Destructors run on a **normal return from `main()`** and on an explicit
  guest **`exit()`** call, in that same atexit-handlers-then-destructors
  order either way. `_Exit()` and `quick_exit()` do **not** run destructors
  (or atexit/at_quick_exit handlers) — matching GCC, which documents
  destructors as running "after `main` completes or `exit` is called", and
  ISO C (C23 7.24.4.4/7.24.4.7: `_Exit`/`quick_exit` terminate without
  running atexit handlers). `abort()` does not run them either.
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

**`-c=native`:** the attribute is lowered verbatim (as a prefix on the
declarator, both on the forward declaration and the definition) — the real
host `cc`'s own libc startup/exit machinery drives it there, not CCCC's own
`cc_run_at` ordering code. The VM's documented ordering above was checked
directly against real GNU/clang semantics and already matches.

---

### `__attribute__((nonnull))` / `__attribute__((nonnull(N,...)))` / `[[gnu::nonnull]]` and `__attribute__((returns_nonnull))` / `[[gnu::returns_nonnull]]`

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
dataflow: each live branch of an `if`/ternary/`&&`/`||` is walked
independently and the resulting null-states are joined at the merge point;
a `for`/`while`/`do` loop is walked as a bounded back-edge fixpoint (a
handful of iterations is always enough to converge, given the null-state
lattice's small height), with every `break`/`continue` site's state tracked
separately and joined in at the loop's exit/header respectively — a
`do`-loop's exit correctly excludes the zero-trip case, since its body
always runs at least once; a `switch` is a single pass with no fixpoint
needed, where each `case` label joins the switch's entry state into the
fall-through state (so `case 0: p = 0; /* fall */ case 1: use(p);` reports
maybe-null at `case 1`, matching real per-case dispatch), `break` states are
joined into the exit, and — when there's no `default` — the pre-switch state
is *also* a live exit predecessor, since the whole switch is then skippable.
This applies to `-Wnonnull` too, not just `-Wmaybe-nonnull`: a loop/switch
that is provably null on every live path (no conditional branch involved)
now warns under plain `-Wnonnull`, where it previously fell back to the
conservative "reset to unknown on exit" scheme and stayed silent. A
construct the fixpoint/join scheme can't safely model — a `goto`/label
anywhere inside it (a jump target from an unknown predecessor), a computed
goto, or a `case` label reachable from a loop body without an intervening
`switch` of its own (Duff's device) — falls back to that original
conservative scheme instead, which can only miss a warning, never fabricate
one. `-Wmaybe-nonnull` has a higher false-positive rate on real code than
plain `-Wnonnull` regardless, so it is never implied by `-Wall` or
`-Wextra` — pass it explicitly.

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

### `__attribute__((sentinel))` / `__attribute__((sentinel(N)))` / `[[gnu::sentinel]]`

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

### `__attribute__((alloc_size(n)))` / `__attribute__((alloc_size(n,m)))` / `[[gnu::alloc_size]]`

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

### `__attribute__((malloc))` / `[[gnu::malloc]]`

Marks a function as returning a fresh, non-aliasing pointer (matching GCC's
`malloc` attribute). Parsed and stored on the function type, but currently
informational only — CCCC does not yet exploit it for aliasing optimizations,
and it deliberately does **not** imply `nonnull`/`returns_nonnull` (a real
allocator can return `NULL` on failure). libc's `malloc`, `calloc`, and
`aligned_alloc` carry it in `include/stdlib.h`; `realloc`/`reallocarray` do
not, since they may return the same block as their input pointer.

The VM has no optimiser, so there is nothing for the attribute to feed:
GCC/Clang exploit it to reorder loads/stores around a call and elide dead
stores, all of which the VM never did. Under `-c=native` the attribute is
serialized verbatim and the host cc acts on it.

### `__attribute__((designated_init))` / `[[gnu::designated_init]]`

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

### `single` / `array` / `ntarray` / `count` / `byte_count` / `bounds` (CCCC-specific)

A Checked C-inspired checked-pointer type and bounds-declaration layer
(#770/#482/#483). Unlike every other attribute in this file, these attach in
**post-`*` qualifier position** — the same grammar slot as `const`/
`volatile`/`restrict` — not declspec position, since a C23 attribute in
declspec position would qualify the pointee, not the pointer:

```c
int  * [[cccc::single]]                    p;  // exactly one object
int  * [[cccc::array, cccc::count(n)]]     a;  // n elements from p's own value
char * [[cccc::ntarray, cccc::count(n)]]   s;  // like array, +1 for a terminator slot,
                                                //   which must stay null/all-zero-bytes
                                                //   (`CHKNT`/`CHKNTZ`, #923/#939)
int  * [[cccc::array, cccc::bounds(lo,hi)]] r; // explicit absolute range
```

`__attribute__((...))` and `@single`/`@array`/`@ntarray`/`@count(n)`/
`@byte_count(n)`/`@bounds(lo,hi)` all work too.

`single` rejects all pointer arithmetic on that pointer as a compile error —
this compile-time rule is always on, independent of the runtime flag below.
A bounds form (`count`/`byte_count`/`bounds`) requires `array` or `ntarray`;
it is a compile error on `single` or with no checked kind. Bounds may
reference any other in-scope parameter (including a later one), local, or
global; on a struct/union member (#921), a bounds expression may also
reference a sibling field, resolved relative to whichever instance is
actually accessed — but not an enclosing local or a bit-field sibling
(compile errors). A bounds expression must be side-effect-free (also a
compile error otherwise — it is re-evaluated at every checked access, not
once).

Runtime enforcement (the `CHKR` opcode, plus `CHKNT`/`CHKNTZ` guarding a
non-null/non-all-zero-bytes write into `ntarray`'s widened terminator slot,
#923/#939 — including through a propagated pointer, #943 — and `CHKAB`
verifying a checked-rooted assignment's source bounds imply an
already-declared-checked target's own bounds, #944, Checked C's
`_Assume_bounds_cast` direction) is gated behind `--checked-pointers` /
`#pragma cccc config(checked_pointers = true)` —
opt-in, not part of any `-0`/`-1`/`-2`/`-3` preset. Full reference, including
the bounds-carry-within-an-expression semantics, the whole-function
propagation rule that lets an interior pointer assigned into a plain local
(`int *q = p + k;`) stay checked too (#919), and why this exists
(`--bounds-checks`/`CHKB` has no upper bound at all for a stack or global
array): [SAFETY.md § Checked Pointers](SAFETY.md#checked-pointers).

Enforcement is VM-only: `-c=native`/`-m`/`-c=generated` warn and drop
`--checked-pointers` rather than enforcing it, but the six attributes are
always parsed, type-checked, and stripped from that output regardless (ABI-
transparent, #482/#488) — see [SAFETY.md § Checked
Pointers](SAFETY.md#checked-pointers) for the full native/serialized-output
note.

---

## Parsed but Ignored

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

## Open Tickets

| # | Attribute | Priority | Description |
|---|-----------|----------|-------------|
| [#215](https://todo.sr.ht/~takeiteasy/cccc/215) | Catch-all | medium | Remaining GNU builtins and attributes |
| [#657](https://todo.sr.ht/~takeiteasy/cccc/657) | 14 architecturally-inert GNU attributes | low | Register in `known_attrs[]` for `__has_attribute` (done) |

## `@`-prefix Attribute Syntax

CCCC supports a concise `@name` / `@name(args)` shorthand that rewrites to
the canonical attribute form before parsing:

| Usage | Rewrites to | Example |
|-------|-------------|---------|
| `@name` (CCCC-specific) | `[[cccc::name]]` | `@comptime`, `@test`, `@test_setup`, `@single`, `@array`, `@ntarray`, `@count(n)`, `@byte_count(n)`, `@bounds(lo,hi)` |
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

Generated and preprocessed output never includes CCCC-specific syntax by
default. `-E`, `-m`, `-c=generated`, and `-c=native` strip CCCC-only
attributes and route markers before emitting C for another compiler. Use
`--attr-target=auto|c23|gnu|msvc|strip` to select how remaining attributes
are printed. `auto` emits standard C23 attributes as `[[...]]` in C23 mode
and uses GNU `__attribute__((...))` otherwise; GNU-only attributes such as
`packed` stay GNU in `auto` mode.

Pass `--emit-cccc` to invert this: `[[cccc::...]]` attributes and route
markers are preserved verbatim, cccc-only `#include`d files are re-emitted
instead of dropped, and checked-pointer qualifiers
(`[[cccc::single/array/ntarray]]`, `count()`/`byte_count()`/`bounds()`) are
serialized in their post-`*` declarator position instead of being omitted.
`[[cccc::test]]`/`comptime`/`test_setup`/`test_teardown`/`build` are
consumed structurally during preprocessing regardless of this flag (they
trigger real compiler behaviour, not just cosmetic attribute spelling) --
`--emit-cccc` only affects attributes that would otherwise be stripped
cosmetically, such as `[[cccc::emit]]`. With `-c=native`, `--emit-cccc`
still hands the (dialect) source to a real system compiler, but disables
the usual `cc`/`clang`/`gcc` PATH search: `CCCC_NATIVE_CC` must name a
compiler explicitly, since a plain `cc` cannot parse `[[cccc::...]]`
syntax (it degrades gracefully to an ignored unknown-attribute warning
under Clang/GCC's C23 attribute handling, but is not guaranteed to under
every compiler).

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

## Position in Grammar

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

## GNU `asm("symbol")` Labels

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

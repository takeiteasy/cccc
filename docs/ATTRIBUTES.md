# Attribute Support

JCC supports GNU `__attribute__((...))` and C23 `[[...]]` attribute syntaxes. Most attributes are **parsed and accepted** but currently have no semantic effect. A subset of commonly-used diagnostic and layout attributes are fully implemented.

## Quick Reference

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
| *all others* | Both | ~ | Parsed and silently ignored |
| `macro` | GNU | ✓ | JCC-specific; compile-time macro (see [MACROS.md](MACROS.md)) |
| `comptime` | GNU | ✓ | JCC-specific; compile-time variable evaluation (see [MACROS.md](MACROS.md)) |

## Supported Attributes

### `__attribute__((aligned(N)))`

Sets minimum alignment for a type or variable. The argument is a constant expression specifying the alignment in bytes. Can also be used without an argument (`__attribute__((aligned))`) to request maximum useful alignment.

**Source:** `src/parse.c:3945-3954`

```c
struct __attribute__((aligned(16))) vec4 { float x, y, z, w; };
int __attribute__((aligned(64))) cache_line;
```

### `__attribute__((packed))`

Prevents the compiler from inserting padding between struct/union members, and can also prevent alignment-based padding at the end of a struct.

**Source:** `src/parse.c:3938-3942`, `src/parse.c:4140-4163`

```c
struct __attribute__((packed)) {
    char c;
    int i;  // directly follows c with no padding
};
```

### `__attribute__((unused))` / `__attribute__((__unused__))` / `[[maybe_unused]]`

Suppresses `-Wunused` warnings on variables, functions, parameters, typedefs, and labels. Both the GNU and C23 forms (`[[maybe_unused]]`) are recognised with full semantic effect.

**Source:** `src/parse.c:3956-3976` (GNU), `src/parse.c:4031` (C23)

```c
int __attribute__((unused)) x;       // GNU
int [[maybe_unused]] y;              // C23
__attribute__((unused)) static void helper(void) {}
```

### `__attribute__((deprecated))` / `__attribute__((__deprecated__))` / `[[deprecated]]`

Marks a declaration as deprecated. Warnings are emitted via `-Wdeprecated` when the identifier is used. Supports an optional message string that is included in the warning output.

**Source:** `src/parse.c:3957-3976` (GNU), `src/parse.c:4032-4040` (C23), `src/parse.c:199-206` (warning emission), `src/parse.c:4977-4990` (use-site checks)

```c
int __attribute__((deprecated("use bar instead"))) old_func(void);
int [[deprecated]] legacy_var;
```

### `__attribute__((macro))` / `__attribute__((comptime))` (JCC-specific)

These are JCC's own extensions for compile-time metaprogramming. They are intercepted by the preprocessor and do not reach the general attribute parser. See [MACROS.md](MACROS.md) for details.

**Source:** `src/preprocess.c:1737-1796`

```c
[[jcc::macro]] int square(int x) { return x * x; }
__attribute__((comptime)) const int version = 42;
```

## Parsed but Ignored

Any GNU `__attribute__` identifier that is not explicitly handled (i.e., not `packed`, `aligned`, `unused`/`__unused__`, or `deprecated`/`__deprecated__`) is **consumed and silently ignored**. The parser skips the attribute name and any parenthesised argument list, then continues.

**Source:** `src/parse.c:3979-3997`

Similarly, any C23 `[[...]]` attribute other than `maybe_unused` or `deprecated` is **consumed and silently ignored** by the C23 attribute parser.

**Source:** `src/parse.c:4010-4058`

Ignored attributes include (but are not limited to):

| Attribute | Syntax | Notes |
|-----------|--------|-------|
| `nodiscard` | C23 | Ticket [#219](https://todo.sr.ht/~takeiteasy/jcc/219) |
| `fallthrough` | C23 | Also requires statement-level attribute support; ticket [#219](https://todo.sr.ht/~takeiteasy/jcc/219) |
| `no_unique_address` | C23 | Ticket [#219](https://todo.sr.ht/~takeiteasy/jcc/219) |
| `noreturn` | C23/GNU | Ticket [#216](https://todo.sr.ht/~takeiteasy/jcc/216) |
| `pure` | GNU | Ticket [#217](https://todo.sr.ht/~takeiteasy/jcc/217) |
| `const` | GNU | Ticket [#217](https://todo.sr.ht/~takeiteasy/jcc/217) |
| `cleanup` | GNU | Ticket [#218](https://todo.sr.ht/~takeiteasy/jcc/218) |
| `format(printf,...)` | GNU | Ticket [#214](https://todo.sr.ht/~takeiteasy/jcc/214) |
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

## Open Tickets

| # | Attribute | Priority | Description |
|---|-----------|----------|-------------|
| [#214](https://todo.sr.ht/~takeiteasy/jcc/214) | `format(printf, fmt, args)` | medium-high | Type-check printf/scanf format strings at compile time |
| [#216](https://todo.sr.ht/~takeiteasy/jcc/216) | `noreturn` / `[[noreturn]]` | high | Mark functions that never return; integrate with control-flow analysis |
| [#217](https://todo.sr.ht/~takeiteasy/jcc/217) | `pure` / `const` | medium | Side-effect-free function annotations for optimisation |
| [#218](https://todo.sr.ht/~takeiteasy/jcc/218) | `cleanup(func)` | medium | Scope-based cleanup callbacks (RAII-style) |
| [#219](https://todo.sr.ht/~takeiteasy/jcc/219) | `nodiscard`, `fallthrough`, `no_unique_address` | medium | Remaining standard C23 attributes |

A catch-all ticket ([#215](https://todo.sr.ht/~takeiteasy/jcc/215)) tracks remaining GNU builtins and attributes.

## Position in Grammar

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

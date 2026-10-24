# Attributes

GNU `__attribute__`, C23 `[[...]]` and the `@name` shorthand. The common diagnostic and layout attributes are implemented; the rest are parsed and ignored.

Status: **yes** has full semantics, **~** is parsed and mostly informational.

## Quick reference

| Attribute | Syntax | Status | Effect |
|---|---|---|---|
| `aligned(N)`, `packed` | GNU | yes | Layout ([attributes-layout.md](attributes-layout.md)) |
| `unused`, `maybe_unused` | GNU, C23 | yes | Suppress `-Wunused` |
| `deprecated`, `deprecated("msg")` | GNU, C23 | yes | `-Wdeprecated`, with the message |
| `comptime`, `macro` | CCCC | yes | Compile-time execution ([attributes-cccc.md](attributes-cccc.md), [macros.md](macros.md)) |
| `format(printf\|scanf, ...)` | GNU | yes | Format-string checks ([attributes-format.md](attributes-format.md)) |
| `nodiscard`, `warn_unused_result` | C23, GNU | yes | `-Wnodiscard` (in `-Wall`) |
| `fallthrough` | C23 | yes | Suppresses `-Wfallthrough` (in `-Wextra`) |
| `noreturn` | C23, GNU | yes | `BTRAP` after calls; warns on returns |
| `error("msg")`, `warning("msg")` | GNU | yes | Compile-time error or warning when called, suppressed in statically dead code (dead `if`/`else` branches, `while(0)` bodies, short-circuit operands, ternary and elvis branches), which enables the `_FORTIFY_SOURCE` `__chk_fail` idiom |
| `nonnull`, `returns_nonnull` | GNU, C23 | yes | [attributes-nonnull.md](attributes-nonnull.md) |
| `method(T[, name])` | CCCC | yes | Struct methods called as `s.f(x)` ([methods.md](methods.md)) |
| `operator(sym)` | CCCC | yes | Operator overloading for structs ([operators.md](operators.md)) |
| `cleanup(fn)`, `constructor`, `destructor` | GNU | yes | [attributes-cleanup.md](attributes-cleanup.md) |
| `sentinel`, `alloc_size`, `designated_init` | GNU | yes | [attributes-gnu.md](attributes-gnu.md) |
| `pure`, `const`, `malloc` | GNU | ~ | Informational in the VM, forwarded under `-c=native` ([attributes-gnu.md](attributes-gnu.md)) |
| `optimize(...)` | GNU, CCCC | ~ | Syntax-checked, then ignored with a warning |
| `single`, `array`, `ntarray`, `count`, `byte_count`, `bounds` | CCCC (after `*`) | yes | Checked pointers ([attributes-bounds.md](attributes-bounds.md)) |
| `assume`, `dynamic` | CCCC (cast type name) | yes | Bounds casts |
| `checked`, `unchecked` | CCCC (function or block) | yes | Regions that ban unchecked pointers, always on |
| `kernel` | CCCC (function) | yes | Rejects anything a Metal or CUDA kernel cannot run ([attributes-kernel.md](attributes-kernel.md)) |
| `global`, `local`, `constant`, `private`, `generic` | CCCC | yes | Address spaces ([attributes-kernel.md](attributes-kernel.md#address-spaces)) |
| `vector_size(N)`, `cccc_visible_lanes(N)` | GNU | yes | Vectors ([attributes-kernel.md](attributes-kernel.md#vector-size)) |
| everything else | both | ~ | Parsed and ignored ([attributes-ignored.md](attributes-ignored.md)) |

## Pages

| Page | Covers |
|---|---|
| [attributes-feature-tests.md](attributes-feature-tests.md) | `__has_attribute`, `__has_feature` and the other `__has_*` operators |
| [attributes-layout.md](attributes-layout.md) | `aligned`, `packed`, `unused`, `deprecated` |
| [attributes-format.md](attributes-format.md) | `format` |
| [attributes-cccc.md](attributes-cccc.md) | CCCC-specific `comptime`, `emit` and include forms |
| [attributes-gnu.md](attributes-gnu.md) | `pure`, `const`, `optimize`, `sentinel`, `alloc_size`, `malloc`, `designated_init` |
| [attributes-cleanup.md](attributes-cleanup.md) | `cleanup`, `constructor`, `destructor` |
| [attributes-nonnull.md](attributes-nonnull.md) | `nonnull`, `returns_nonnull` |
| [attributes-bounds.md](attributes-bounds.md) | Pointer bounds, casts, regions |
| [attributes-kernel.md](attributes-kernel.md) | The GPU kernel subset |
| [attributes-syntax.md](attributes-syntax.md) | `@` shorthand, grammar position, `asm` labels |
| [attributes-ignored.md](attributes-ignored.md) | Parsed and ignored attributes |

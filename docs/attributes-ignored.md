# Parsed but Ignored Attributes

Attributes accepted without effect.

## Parsed but Ignored

Any GNU `__attribute__` identifier without compiler semantics is consumed: the parser skips the name and any parenthesised argument list, then continues. What happens next depends on whether `__has_attribute` recognizes the name:

| Attribute | Example | Result |
|-----------|---------|--------|
| Recognized by `__has_attribute` | `__attribute__((cold))`, `[[gnu::noinline]]` | Accepted silently |
| Unknown name | `__attribute__((bogus))`, `[[gnu::bogus]]` | `-Wattributes` warning |
| Unscoped non-standard C23 name | `[[cold]]` | `-Wattributes` warning |

This matches GCC and Clang: a header that checks `__has_attribute(cold)` before using it compiles cleanly under `-Wall -Werror`.

Ignored attributes include (but are not limited to):

| Attribute | Syntax | Tracking |
|-----------|--------|----------|
| `no_unique_address` | C23 | Parsed but ignored — VM optimisation deferred |
| `visibility` | GNU | Recognized by `__has_attribute` (no ELF, so no-op) |
| `section` | GNU | Recognized by `__has_attribute` (no object-file sections) |
| `weak` | GNU | Silently ignored: a weak symbol links as a strong one, so an undefined weak function or variable is an error rather than a null address, and a strong definition elsewhere does not override a weak one. Tracked by [#2139](https://todo.sr.ht/~takeiteasy/cccc/2139) |
| `weakref` | GNU | |
| `alias` | GNU | Recognized by `__has_attribute` (no linker) |
| `target` | GNU | Recognized by `__has_attribute` (no per-function ISA codegen) |
| `hot` / `cold` | GNU | Recognized by `__has_attribute` (no branch-temperature layout) |
| `always_inline` / `flatten` / `noinline` | GNU | Recognized by `__has_attribute` (no inliner in the JIT) |
| `gnu_inline` | GNU | On an `extern inline` definition: inline-only, compiled only when called. A later non-inline definition in the same file, or a real definition in another file, takes precedence. `-c=native` gives a called one a weak out-of-line copy |
| `used` | GNU | Recognized by `__has_attribute` (no symbol-level DCE) |
| `may_alias` | GNU | Recognized by `__has_attribute` (no strict-aliasing optimizer) |
| `mode` | GNU | Recognized by `__has_attribute` (no machine-mode type system) |
| `transparent_union` | GNU | Recognized by `__has_attribute` (no union-arg coercion modeling) |
| `alloc_align` | GNU | |
| `format_arg` | GNU | |
| `unsequenced` | C23 | |
| `reproducible` | C23 | |

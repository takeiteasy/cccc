# Comptime Builtins

Contributor notes: how comptime builtins are declared and registered.

The macro API is private to macro compilation. CCCC embeds its own `cccc/reflection.h`
and injects it automatically while macro and comptime helper functions are
compiled, but that bundled header is not on the public include path. Macro code
can use the `$*`, `Quote*`, `MacroErrorAt`, `Gensym`, and `$dump_*`
convenience macros directly.

`include/cccc/reflection.h` is the single source of truth for every comptime
builtin's C signature. `src/reflection.c` and `src/macros.c` include its
prototypes (`CCCC_REFLECTION_PROTOTYPES_ONLY` hides the macro-program-only
parts), so a definition that drifts from its prototype is a compile error.

A builtin is registered by one line above its definition:

```c
CC_BUILTIN(__builtin_ast_type_size, 1);
int __builtin_ast_type_size(Type *ty) { ... }
```

| Macro | Use |
|---|---|
| `CC_BUILTIN(fn, n)` | `n` arguments, integer or pointer return |
| `CC_BUILTIN_VARIADIC(fn, n)` | `n` fixed arguments before `...` |
| `CC_BUILTIN_DOUBLE(fn, n)` | `n` arguments, `double` return |

Adding a builtin means a prototype in `reflection.h`, the definition in
`src/reflection.c` (or `src/macros.c`, for the few that need direct compiler
state), and its `CC_BUILTIN` line.

`reflection.h` also hand-copies three of the compiler's internal enums for
macro code to use: `TypeKind` (`TK_*`), `NodeKind` (`NK_*`, a deliberate
subset), and `AttrTargetKind` (`ATTR_TARGET_*`, an exact copy). Their
numeric values must match `src/cccc.h`'s internal `TypeKind`/`NodeKind`/
`AttrTargetKind` exactly.

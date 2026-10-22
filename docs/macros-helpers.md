# Comptime Helpers

Helper functions and comptime blocks. Routing headers and mode-conditional directives are in [macros-includes.md](macros-includes.md).

## Helpers

Every `@comptime` function is callable from user code; a plain helper used only by other comptime functions takes the same attribute. All of them compile together, so they can call each other whatever the order. `__attribute__((comptime))` is the GNU spelling.

```c
@comptime int plus_one(int n) { return n + 1; }

@comptime Node *make_value(void) {
    return MakeIntLiteral(plus_one(41));
}

int main(void) { return make_value(); }
```

The old `[[cccc::macro]]`, `__attribute__((macro))` and `@macro` are errors that point to `comptime`.

## Comptime blocks

`#pragma cccc comptime begin` / `end` makes every declaration in between implicitly comptime:

```c
#pragma cccc comptime begin
#include <glob.h>              // comptime-only inside the block
int glob_count(const char *pat, int flags) {
    glob_t g;
    glob(pat, flags, NULL, &g);
    int n = (int)g.gl_pathc;
    globfree(&g);
    return n;
}
int helper = 7;                // a comptime variable
#pragma cccc comptime end

int main(void) { ... }         // back to runtime code
```

The `glob` call reads the filesystem, so it needs `--comptime-allow=fs` ([comptime-policy.md](comptime-policy.md)).

| Inside a block | Treated as |
|---|---|
| Function definitions | `[[cccc::comptime]]` |
| Variable and struct declarations | Comptime variables |
| `#include` | Comptime-only, invisible to runtime code |
| Directives marked `@emit` | Copied to generated output |
| `#pragma cccc emit begin ... end` | A runtime escape sub-block; several allowed ([macros-emit.md](macros-emit.md#emit-blocks)) |
| Explicit `[[cccc::comptime]]` | Respected; explicit attributes win |

Comptime and emit contexts may alternate, but a second `comptime begin` inside a comptime context (or `emit begin` inside emit) is an error, and an emit context needs an enclosing comptime one.

### Bare form

Omit `begin` to make everything from the pragma to the end of the file comptime. No `end` is allowed.

```c
#include <stdio.h>
#include [[cccc::comptime]] <glob.h>

#pragma cccc comptime       // comptime from here to EOF

int glob_count(const char *pat, int flags) { ... }
int scale = 3;

glob_count("include/*.h", 0);   // file-scope macro call
```

### Headers

A `comptime begin` in a header with no `end` is closed when the header ends and reports `-Wcomptime-block-leak`. The bare form never does.

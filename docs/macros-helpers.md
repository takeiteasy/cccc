# Comptime Helpers

Helper code, blocks and mode-conditional directives that support macros.

## Comptime Helpers

All `[[cccc::comptime]]` functions are entry-callable from user code. A plain
helper that is only called by other comptime functions still uses the same
attribute — the distinction is just whether you call it from user code or not.

```c
[[cccc::comptime]]
int plus_one(int n) {
    return n + 1;
}

[[cccc::comptime]]
Node *make_value(void) {
    return MakeIntLiteral(plus_one(41));
}

int main(void) {
    return make_value();
}
```

All comptime functions are compiled together in a single pass, so they can call
each other even when the callee appears later in the translation unit.

The GNU-attribute equivalent is `__attribute__((comptime))`.

> **Removed:** the old `[[cccc::macro]]` / `__attribute__((macro))` aliases (and
> the `@macro` shorthand) are no longer accepted — using any of them is a
> compile error directing you to `[[cccc::comptime]]`.

### Comptime block

`#pragma cccc comptime begin` / `#pragma cccc comptime end` opens a block where every
declaration is implicitly `[[cccc::comptime]]` — no per-declaration attribute
required.

```c
#pragma cccc comptime begin
#include <glob.h>              // treated as comptime-only inside the block
int glob_count(const char *pat, int flags) {
    glob_t g;
    glob(pat, flags, NULL, &g);
    int n = (int)g.gl_pathc;
    globfree(&g);
    return n;
}
int helper = 7;                // comptime variable
#pragma cccc comptime end

// Back to normal runtime code
int main(void) { ... }
```

The `glob` call above reads the filesystem, so it needs `--comptime-allow=fs` (see [comptime-policy.md](comptime-policy.md)).

Inside a comptime block:
- `#include` directives are queued as comptime-only includes — invisible to the runtime translation unit.
- preprocessor directives marked `@emit` are copied to generated output.
- `#pragma cccc emit begin...end` opens a runtime escape sub-block; multiple sub-blocks are allowed.
- Function definitions are treated as `[[cccc::comptime]]`.
- Variable and struct declarations are treated as comptime variables.
- Existing `[[cccc::comptime]]` annotations are respected; explicit attributes always take precedence.

Nesting rules: alternation between comptime and emit contexts is allowed. A
second `#pragma cccc comptime begin` while a comptime context is active, or a
second `#pragma cccc emit begin` while an emit context is active, is a hard
error. Emit contexts always require an enclosing comptime context.

**Bare form** — omit `begin` to mark everything from the pragma to EOF as
comptime. No closing `end` is needed or accepted; the bare form always runs to
the end of the file:

```c
#include <stdio.h>
#include <string.h>
#include [[cccc::comptime]] <glob.h>

#pragma cccc comptime       // everything from here to EOF is comptime

int glob_count(const char *pat, int flags) {
    glob_t g;
    glob(pat, flags, NULL, &g);
    int n = (int)g.gl_pathc;
    globfree(&g);
    return n;
}
int scale = 3;

glob_count("include/*.h", 0);  // file-scope macro call
```

**Auto-close in headers** — if a `#pragma cccc comptime begin` block in a header
omits `#pragma cccc comptime end`, the block is closed automatically when the
header ends and a `[-Wcomptime-block-leak]` warning is emitted. The bare form
never triggers this warning.

### Include scoping

By default, headers included with a plain `#include` are **runtime-only**: their
declarations and `#define` macros are visible to runtime code but are not
forwarded to comptime functions. Three annotation forms control which context a
header reaches:

| Form | Runtime TU | Comptime pass |
|------|-----------|---------------|
| `#include <header>` | ✓ | ✗ (default) |
| `#include @comptime <header>` | ✗ | ✓ |
| `#include @shared <header>` | ✓ | ✓ |
| `#define NAME ...` | ✓ | ✗ (default) |
| `#define @shared NAME ...` | ✓ | ✓ |

This table describes headers whose *own* `#define` macros reach the comptime
pass through this routing, and headers a comptime body needs *without* also
landing in the runtime TU (`@comptime`). It is not the whole story for
regular `#include`: a plain, unrouted `#include`d header's **declarations**
(types, tags, prototypes, enum constants) already reach the comptime pass on
demand, whether or not that header defines any `[[cccc::comptime]]` code of
its own — see
[Pre-parse macro declaration context](macros-execution.md#what-comptime-code-can-see).
`@comptime`/`@shared` are still necessary for a header's `#define` macros
(never forwarded to comptime automatically) and for keeping a comptime-only
header out of the runtime TU entirely; they are **not** needed just to make a
declaration-only header's types visible to comptime code any more.

`-c=native` re-emits a plain `#include`d file's own `#include` line verbatim
for the downstream system compiler; a file that itself uses this routing
syntax is never re-emitted that way, since none of it means anything outside
CCCC.

All three forms accept the three interchangeable spelling styles:

```c
#include @shared <glob.h>
#include [[cccc::shared]] <glob.h>
#include __attribute__((shared)) <glob.h>
```

**`@comptime`** — feeds a header into the comptime unit only. The header and any
types it defines are invisible to the runtime translation unit.

```c
#include @comptime <glob.h>
// Equivalent: #include [[cccc::comptime]] <glob.h>

[[cccc::comptime]]
int glob_struct_nonempty(void) {
    return (int)sizeof(glob_t) > 0;  // glob_t is available here
}

// glob_t is NOT defined for ordinary runtime code below this point
int main(void) {
    return glob_struct_nonempty() ? 42 : 1;
}
```

**`@shared`** — includes the header in both the runtime translation unit and the
comptime pass. Use this when comptime functions need types, prototypes, or
`#define` macros from a header that runtime code also uses.

```c
#include @shared <glob.h>

[[cccc::comptime]]
int glob_struct_nonempty(void) {
    return (int)sizeof(glob_t) > 0;  // available in comptime
}

int main(void) {
    glob_t g;         // also available in runtime
    (void)g;
    return glob_struct_nonempty() ? 42 : 1;
}
```

`@shared` is valid on `#include` and `#define` (see
[Macro isolation between comptime functions](macros-execution.md#macro-isolation)
for `#define @shared`'s per-macro semantics). Applying it to any other
directive (e.g. `#undef @shared`, `#ifdef @shared`) is a compile error.

The conventional C opaque-handle idiom -- forward-declare a struct and
typedef it to its own name (`typedef struct Foo Foo;`) -- works normally in
a `@shared` header, including when the file also defines
`[[cccc::comptime]]` functions:

```c
/* handle.h */
typedef struct Foo Foo;
Foo *foo_make(void);
```

```c
#include @shared "handle.h"

[[cccc::comptime]]
Node *check(void) { return MakeIntLiteral(0); }

int main(void) {
    Foo *f = foo_make();
    (void)f;
    return check();
}
```

**`--comptime-include-all`** — global flag with two effects: widen declaration
resolution to **system headers** (non-system headers already resolve on
demand without it), and forward all `#define` macros to comptime without
needing per-include/per-macro `@shared` annotations.

**Emit includes** — `#include [[cccc::emit]]` routes an include to serialized
generated output only (see
[Emit directives and includes in generated output](macros-emit.md#what--cgenerated-captures)).

> **Note:** `@comptime` on non-include directives (e.g. `#define`, `#ifdef`)
> continues to work as before, routing the directive to the comptime
> preprocessing context. `@shared` on `#define` is different: it does not
> route the macro anywhere else — it opts that one macro into the isolated
> comptime macro table while leaving it in the runtime TU as well. `@shared`
> on any directive other than `#include` or `#define` is rejected.

### Mode-conditional directives (`@build` / `@test`)

`@build` and `@test` route attributes gate **any** preprocessor directive on
whether `--build` or `--testing` mode is active. When the mode is inactive the
directive is silently skipped; when it is active, the route token is stripped
and the directive is processed normally.

```c
// Applied only when --build is active:
#define @build BUILD_JOBS 8

// Applied only when --testing is active:
#define @test  TEST_TIMEOUT_MS 5000

// Conditional blocks gate entire regions:
#ifdef @build BUILD_JOBS
#  define JOBS BUILD_JOBS
#endif                          // plain #endif — no route required
```

All three attribute spellings are accepted:

```c
#define @build            BUILD_JOBS 8
#define [[cccc::build]]   BUILD_JOBS 8
#define __attribute__((build)) BUILD_JOBS 8
```

**Conditional-nesting rules**: `#ifdef @build` / `#ifndef @build` / `#if @build`
desugar to `#if 0` when the mode is inactive, keeping the conditional stack
balanced so that a plain `#endif` (without a route) can close them.  The
`#else` branch under an inactive `@build`/`@test` conditional **runs** normally:

```c
#ifdef @build BUILD_JOBS
    /* skipped — build mode is off */
#else
    /* runs in comp and testing modes */
#endif
```

`@build`/`@test` can also appear on `#undef`, `#include`, and other directives
— all are silently dropped when the mode is inactive.  `#else` and `#endif`
with a route attribute have the route stripped and are always processed.

This is the first-class alternative to `#ifdef __CCCC_BUILD_MODE__` /
`#ifdef __CCCC_TEST_MODE__` guards and composes cleanly with the `@build` /
`@test` routing on `#include` directives already supported.

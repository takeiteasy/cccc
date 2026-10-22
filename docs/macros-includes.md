# Include Routing and Mode Directives

Choose which context a header or directive reaches, and gate directives on `--build` or `--testing`.

## Include scoping

| Form | Runtime | Comptime |
|---|---|---|
| `#include <header>` | yes | declarations on demand; `#define`s no |
| `#include @comptime <header>` | no | yes |
| `#include @shared <header>` | yes | yes |
| `#define NAME ...` | yes | no |
| `#define @shared NAME ...` | yes | yes |

A plain `#include`'s types, tags, prototypes and enum constants already reach comptime on demand ([macros-execution.md](macros-execution.md#what-comptime-code-can-see)). Routing is needed for a header's `#define`s, and to keep a comptime-only header out of the runtime unit. The three spellings are interchangeable:

```c
#include @shared <glob.h>
#include [[cccc::shared]] <glob.h>
#include __attribute__((shared)) <glob.h>
```

`-c=native` re-emits a plain `#include` line for the downstream compiler; a file using routing syntax is never re-emitted that way.

### `@comptime`

The header and its types exist only in the comptime unit:

```c
#include @comptime <glob.h>

@comptime int glob_struct_nonempty(void) {
    return (int)sizeof(glob_t) > 0;   // glob_t is available here
}

int main(void) {                      // glob_t is NOT defined here
    return glob_struct_nonempty() ? 42 : 1;
}
```

### `@shared`

The header is in both:

```c
#include @shared <glob.h>

@comptime int glob_struct_nonempty(void) { return (int)sizeof(glob_t) > 0; }

int main(void) {
    glob_t g;            // also available at runtime
    (void)g;
    return glob_struct_nonempty() ? 42 : 1;
}
```

`@shared` is valid only on `#include` and `#define` (per-macro semantics: [macros-execution.md](macros-execution.md#macro-isolation)). On `#define` it does not route the macro elsewhere: it opts that macro into the comptime macro table and leaves it in the runtime unit. On any other directive it is an error.

An opaque-handle idiom (`typedef struct Foo Foo;`) works in a `@shared` header, including alongside comptime functions:

```c
/* handle.h */
typedef struct Foo Foo;
Foo *foo_make(void);
```

```c
#include @shared "handle.h"

@comptime Node *check(void) { return MakeIntLiteral(0); }

int main(void) {
    Foo *f = foo_make();
    (void)f;
    return check();
}
```

### Other routes

| Route | Effect |
|---|---|
| `--comptime-include-all` | Widens declaration resolution to system headers and forwards every `#define` to comptime |
| `#include [[cccc::emit]]` | Routes the include to serialized output only ([macros-emit.md](macros-emit.md#what--cgenerated-captures)) |
| `@comptime` on `#define`, `#ifdef`, ... | Routes the directive to the comptime preprocessing context |

## Mode-conditional directives

`@build` and `@test` gate **any** directive on `--build` or `--testing` being active. When the mode is off the directive is silently skipped; when on, the route token is stripped and the directive runs.

```c
#define @build BUILD_JOBS 8          // only with --build
#define @test  TEST_TIMEOUT_MS 5000  // only with --testing

#ifdef @build BUILD_JOBS
#  define JOBS BUILD_JOBS
#endif                               // a plain #endif closes it
```

| Spelling |
|---|
| `#define @build BUILD_JOBS 8` |
| `#define [[cccc::build]] BUILD_JOBS 8` |
| `#define __attribute__((build)) BUILD_JOBS 8` |

`#ifdef @build`, `#ifndef @build` and `#if @build` become `#if 0` when the mode is off, which keeps the conditional stack balanced, so a plain `#endif` closes them. The `#else` branch **runs** in that case:

```c
#ifdef @build BUILD_JOBS
    /* skipped: build mode is off */
#else
    /* runs in compile and testing modes */
#endif
```

`@build`/`@test` also work on `#undef`, `#include` and other directives. On `#else` and `#endif` the route is stripped and the directive is always processed. It is the first-class alternative to `#ifdef __CCCC_BUILD_MODE__` and `#ifdef __CCCC_TEST_MODE__` guards.

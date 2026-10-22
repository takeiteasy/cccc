# Build Entry and Recipes

How the build entry is found, and recipes embedded in a program file.

## The build entry

The entry is the function the runner invokes. It is identified in precedence
order:

1. **Explicit flag:** `--build-entry=NAME`.
2. **Attribute:** a function tagged `[[cccc::build]]`. Exactly one is required;
   more than one is an ambiguity error listing every candidate.
3. **Default name:** a function named `build_main`.
4. **Every factory:** if none of the above is found but the file declares one
   or more `[[cccc::build_target]]` factories, every factory is invoked and
   the union of their declared targets is built — equivalent to calling
   `BuildAll(ctx)` from an entry that did nothing but invoke each factory.
   This is what makes `cccc --build demo.c` work on a file whose only build
   recipe is an inline `@build_target` next to `main()` — see
   [Discoverable factory functions](build-targets.md#discoverable-factory-functions) below.

A `[[cccc::build]]`-tagged function can be named anything — the tag alone
resolves it. The `build_main` fallback only applies when a script has no
`[[cccc::build]]` attribute and no `--build-entry` flag at all; it is not a
second requirement layered on top of the attribute. The factory fallback only
applies when neither of those exists either.

The attribute accepts the C23 and GNU forms, like `[[cccc::comptime]]`:

```c
[[cccc::build]]                     // C23
__attribute__((build))              // GNU
__attribute__((cccc::build))        // GNU namespaced
```

Signature (the build context is passed as the first parameter):

```c
int  build_main(Builder *ctx);   // non-zero return = build failure
void build_main(Builder *ctx);   // success iff all targets built
```

The same file is still valid C: in default mode (`cccc build.c`) the
`[[cccc::build]]` attribute is consumed and the entry is simply not called.

## Build recipes in a program file

A build script does not need its own file. A single `.c` that defines
`main()`, `[[cccc::test]]` functions, and a `[[cccc::build]]`/
`[[cccc::build_target]]` recipe all in one place works exactly as you would
expect from each flag on its own:

```c
[[cccc::build_target]]
BuildTarget *demo(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "demo");
    AddSource(t, "demo.c");
    return t;
}

int main(void) {
    return 0;
}
```

| Command | Behaviour |
|---|---|
| `cccc demo.c` | Runs `main()` on the VM. The build recipe compiles but is never invoked. |
| `cccc -c=native -o demo demo.c` | Native binary of `main()`. The `[[cccc::build_target]]`/`[[cccc::build]]` bodies are never emitted — see [native.md](native.md). |
| `cccc --build demo.c` | Runs the build recipe (here, the entry-less factory fallback above). `main()` is ignored. |
| `cccc --build --run demo.c` | Runs the build recipe, then `main()` on the VM if the build succeeded. A failed build skips `main()` and exits nonzero. |

`building.h` (the `Builder`/`BuildTarget` types and every `Build*`/`Executable`/
etc. macro) is injected automatically whenever the file's own source carries a
`[[cccc::build]]`/`[[cccc::build_target]]` attribute (`@build`/`@build_target`,
`[[cccc::build...]]`, or `__attribute__((build...))`) — you don't need
`--build` just to get the file to parse. This is demand-driven, not
unconditional: a file with no such attribute anywhere sees none of
`building.h`'s ~90 names, so an ordinary program is free to define its own
`Build`/`Executable`/etc. without a collision. The scan only looks at the
file's own text, so an attribute produced by macro expansion, or written only
in an `#include`d header, is not enough by itself — pass `--build` explicitly
in that case, or `#include <cccc/building.h>` directly.

## Mode predefined macros

When CCCC starts it defines exactly one of three mutually-exclusive macros
depending on the active mode:

| Macro | Defined when |
|---|---|
| `__CCCC_BUILD_MODE__` | `--build` is active |
| `__CCCC_TEST_MODE__` | `--testing` is active |
| `__CCCC_COMP_MODE__` | Neither `--build` nor `--testing` (normal compilation) |

Use `#ifdef` / `#ifndef` to branch on these at compile time:

```c
#ifdef __CCCC_BUILD_MODE__
#include "build/extra_targets.h"
#endif
```

## Conditional directives (`@build`)

Any preprocessor directive can be gated on `--build` mode by prefixing it with
a `@build` route attribute.  When `--build` is active the route token is
stripped and the directive is processed normally; when it is inactive the
directive is silently skipped.  All three attribute spellings are accepted:

```c
#define @build            BUILD_JOBS 8     // @-prefix
#define [[cccc::build]]   BUILD_JOBS 8     // C23
#define __attribute__((build)) BUILD_JOBS 8 // GNU
```

This extends to `#include`, `#ifdef`, `#ifndef`, `#undef`, and every other
preprocessor directive:

```c
#include @build "build/extra_targets.h"
#include @build <external/build_helpers.h>

#define @build BUILD_JOBS 8

#ifdef @build BUILD_JOBS
#  define JOBS BUILD_JOBS
#endif                              // plain #endif — no route needed
```

When `--build` is inactive, an `@build #ifdef`/`#ifndef`/`#if` is rewritten
as `#if 0` internally so the conditional stack stays balanced.  A plain
`#endif` (without `@build`) closes it correctly.  The `#else` branch under an
inactive `@build` conditional **runs** normally.

These are the first-class equivalents of the `__CCCC_BUILD_MODE__` guard style:

```c
// Old style                          // New style
#ifdef __CCCC_BUILD_MODE__            #define @build BUILD_JOBS 8
#define BUILD_JOBS 8
#endif

#ifdef __CCCC_BUILD_MODE__            #include @build "build/extra_targets.h"
#include "build/extra_targets.h"
#endif
```

Use them to split large build scripts across multiple files without polluting
normal compilation, and to define build-only constants cleanly.

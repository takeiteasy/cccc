# Emitting Directives and Attributes

Control what reaches generated output: preprocessor directives, emit blocks and attributes on generated functions.

## What `-c=generated` captures

Preprocessor directives in the primary file's runtime code (outside comptime blocks) are captured verbatim at the top of `-c=generated` output: `#include`, `#define`, `#ifdef`/`#endif` and the rest.

```c
#include <string.h>   // outside comptime: captured automatically

#pragma cccc comptime
// ... generated functions that use strcmp ...
```

| Case | Behaviour |
|---|---|
| Extra input files | Contribute no directives: they are comptime-support modules. Route one with `@emit`, or a needed header with `@shared`. |
| `--emit-only` | Capture only directives tagged `[[cccc::emit]]`, `@emit` or `__attribute__((emit))` |
| Repeated emitted `#include` | Deduplicated; other emitted directives keep source order |
| Plain unconditional directive | Always ahead of every generated declaration |
| Directive in a conditional shell | Keeps its interleaved position; an emptied shell is dropped |
| Header reached only via `#include @comptime/@build/@test` | Never replayed: an error names the type. Use `#include @shared`. |
| Nothing published, no ordinary function | `-c=generated` writes a near-empty file; without it: `input defines no functions or variables` |

```c
#include [[cccc::emit]] <string.h>
#include @emit <stdint.h>
#ifdef @emit _WIN32
gen_windows_helpers();
#endif @emit
```

Inside comptime blocks auto-capture does not apply, so mark escapes explicitly:

```c
#pragma cccc comptime begin
#include [[cccc::emit]] <platform.h>   // escapes into output
PublishNode(fn);
#pragma cccc comptime end
```

`--emit-only` and `[[cccc::emit]]` choose *which* directives are captured. `--emit-cccc` is unrelated: it keeps CCCC dialect syntax in the output ([coverage.md](coverage.md#attributes)).

## Emit blocks

Several raw directives, plus ordinary declarations that are compiled into the runtime program *and* copied to the output:

```c
#pragma cccc comptime begin
#pragma cccc emit begin
#ifdef _WIN32
#define CCCC_PLATFORM_WINDOWS 1
#endif
int win_init(void);            // compiled and emitted
#pragma cccc emit end
#pragma cccc comptime end
```

An emit block needs an enclosing comptime context, a comptime block may hold several, and nesting a block in one of its own kind is an error. Macros such as `AssertEq` or `Executable` are **not** expanded inside, so use the `__builtin_*` functions directly.

Functions in an emit block may carry `[[cccc::test]]`, `[[cccc::build]]`, `[[cccc::build_target]]`, `[[cccc::test_setup]]` and `[[cccc::test_teardown]]`; they register as in normal source:

```c
#pragma cccc comptime begin
#pragma cccc emit begin
[[cccc::test]]  void generated_test(void) { }                     // runs under --testing
[[cccc::build]] int generated_build(Builder *ctx) { return 42; }  // runs under --build
#pragma cccc emit end
#pragma cccc comptime end
```

## Attributes on generated functions

`AddAttribute(fn, "...")` accepts any attribute spelling, parsed like source:

```c
@comptime void gen_tests(void) {
    Obj *fn = MakeFunction("my_generated_test", GetType("void"));
    WithFn(fn) { FunctionSetBody(fn, Quote("return;")); }
    PublishNode(fn);
    AddAttribute(fn, "cccc::test(suite=\"generated\", name=\"my test name\", timeout=5000)");
}
gen_tests();
```

```c
AddAttribute(fn, "cccc::build");
AddAttribute(fn, "cccc::build_target(kind=native)");
AddAttribute(fn, "nodiscard(\"check return value\")");
AddAttribute(fn, "__attribute__((noreturn))");
AddAttribute(fn, "@myattr");           // custom attribute handler
```

`MarkAsTest(fn)`, `MarkAsBuild(fn)` and `MarkAsBuildTarget(fn, kind)` are shorthands; use `AddAttribute` for test options or standard attributes.

## Directives while a macro runs

```c
@comptime void gen(void) {
    EmitDirective("#ifdef _WIN32");
    Obj *fn = MakeFunction("win_helper", GetType("int"));
    FunctionSetBody(fn, Quote("return 42;"));
    PublishNode(fn);
    EmitDirective("#endif");
}
```

## Routing directives

An attribute after a directive keyword sends it to the comptime stream:

```c
#define @comptime CT_VALUE 42
#ifdef @comptime CT_VALUE
#define @comptime CT_SEEN 1
#endif @comptime
```

Writing `@directive` sends it to the *opposite* context:

| Written in | `@define FOO` becomes |
|---|---|
| Runtime code | `#define @comptime FOO` |
| Comptime block | `#define @emit FOO` |
| Emit block | `#define @comptime FOO` |

```c
@define CT_VALUE 42      // #define @comptime CT_VALUE 42
@ifdef CT_VALUE
@define CT_SEEN 1
@endif
```

Works with `define`, `undef`, `if`, `ifdef`, `ifndef`, `elif`, `else`, `endif`, `include`, `pragma`, `error`, `warning` and `line`.

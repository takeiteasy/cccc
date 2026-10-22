# Macro Execution Model

When comptime functions run, and what they can see.

## Execution forms

| Form | Declared as | Runs | Return value |
|---|---|---|---|
| Global generation | `void gen(char *a1, ...)`, called at file scope | Before the main parse | Side effects only; a block splices its declarations into file scope[^block]. Arguments arrive stringified. |
| Call-site expansion | `Node *gen(Node *a1, ...)`, called in code | During macro expansion, after parsing | Replaces the call |
| Custom attribute | `void gen(AttrTarget *t, ...)`, used as `@name` | During the parse, after the target is built | Ignored; generated declarations are side effects |

## What comptime code can see

Global generation compiles before the main parse. Declarations resolve **on demand**: a name is pulled into the comptime program only when comptime code uses it, directly, through a `Quote` template, or through a lookup such as `GetType("T")`.

| Item | Visible to comptime code |
|---|---|
| Types, tags, enumerators, prototypes, `extern` declarations | From any non-system file in the build |
| Function definitions | By signature; the body is pulled in only if comptime code calls it or takes its address[^body] |
| Uninitialized global object | From the main file, or a file that defines comptime code |
| Initialized global object | Only from the main file, with a literal-only initializer[^snapshot] |
| `#define` macros | Only `-D`, `@shared` and `reflection.h` macros |
| System headers (`<glob.h>`) | Only with `--comptime-include-all`, or `@shared`/`@comptime` routing |

```c
static const char *g_name = "widget";   // visible: literal initializer
static const int g_max = 64;            // visible

@comptime Node *gen(void) {
    return MakeIntLiteral((int)strlen(g_name) + g_max);
}
```

A name that is not visible gives `undefined variable`, with a hint at the ways to expose it.

### Sharing `#define` macros

```c
#define @shared AOT_MAX_SCOPE 64   // comptime and runtime
#define AOT_OTHER 1                // runtime only

@comptime Node *gen(void) {
    int floats[AOT_MAX_SCOPE];     // fine
    return MakeIntLiteral(0);
}
```

| To share | Use |
|---|---|
| One macro | `#define @shared NAME ...` |
| A whole header | `#include @shared "h.h"` |
| Everything, including system headers | `--comptime-include-all` |
| From the command line | `-D` (survives a later `#define` of the same name) |

`[[cccc::shared]]` and `__attribute__((shared))` are equivalent to `@shared`.

## Calling code across files

Comptime code can call an ordinary function declared in a header, provided its definition is **one of the files passed to cccc**:

```c
/* helper.h */  int helper_double(int n);
/* helper.c */  int helper_double(int n) { return n * 2; }
```

```c
/* main.c */
#include @comptime "helper.h"

@comptime void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(helper_double(21))));
}
gen();

int main(void) { return f(); }   // 42, computed at compile time
```

```sh
cccc main.c helper.c
```

| Callee | Needs |
|---|---|
| Function defined in an input file | Its declaration visible, and the `.c` on the command line |
| Function in a separately compiled object | Not callable: `undefined function in macro bytecode` |
| Host libc / FFI function | A declaration that reaches comptime (explicit prototype, or `@comptime`/`@shared` header); a plain `#include` does not route it |

Callees are pulled in transitively. A forwarded body that cannot compile in the isolated comptime context is dropped silently unless comptime code calls it, which is then an error.

## Macro isolation

Each comptime function body gets its own macro table, so a `#define` in one body is invisible to the others:

```c
@comptime int helper(void) {
#define LOCAL 1
    return LOCAL;
}

@comptime void generate(void) {
    // LOCAL is not visible here
}
```

`reflection.h`, comptime include/pragma blocks, `@shared` headers and `-D` macros are visible to every body. `--allow-comptime-pp-bleed` makes all bodies share one table.

## Comptime calling comptime

Every comptime function has a prototype before any definition, so mutual recursion needs no forward declaration:

```c
@comptime int is_even(int n) { return n == 0 ? 1 : is_odd(n - 1); }
@comptime int is_odd(int n)  { return n == 0 ? 0 : is_even(n - 1); }
```

A bodyless `@comptime int f(void);` is a no-op; if no definition appears, it is the error `comptime function 'f' declared but never defined`. To forward-declare *generated runtime* functions, see [macros-builders.md](macros-builders.md#function-generation).

[^block]: See [macros-global.md](macros-global.md#returning-a-block). A `Node *`-returning call inside a global's scalar initializer is evaluated after the main parse instead; see [macros-global.md](macros-global.md#comptime-in-global-initializers).
[^body]: A definition resolves by signature like a prototype. This holds wherever the definition lives: the main file, another input, or a file reached through a declaration-only header. Unused declarations cost nothing, however large; a struct pulls in exactly the types it names.
[^snapshot]: Comptime code sees a snapshot of the value; writes do not reach the runtime program, and a `Quote` template cannot name the global (it would refer to the comptime copy). Use the return value, an out parameter, or [comptime variables](macros-variables.md#comptime-variables). A declaration-only `extern` brought in with `#include @shared` has no storage in either program, so a template may name it and the linker resolves it. Rationale: [macros-impl.md](macros-impl.md#visibility-of-globals).

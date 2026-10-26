# Checked Regions

Say "only checked pointers in here" for a block, a function or a stretch of file, so code can migrate piece by piece.

Inside a checked region an unchecked pointer declaration, a plain array declaration or an unsafe pointer cast is a compile error, or a warning once you [downgrade it](#downgrading-to-warnings).

## Opening a region

```c
[[cccc::checked]] void f(void) {
    int * [[cccc::array, cccc::count(n)]] a = ...;  // fine
    int *p = a;                                     // error: unchecked local
    int buf[8];                                     // error: plain array
    int ok _Checked[8];                             // fine

    [[cccc::unchecked]] {
        int *raw = (int *)a;                        // fine: escape hatch
    }
}
```

```c
#pragma cccc checked begin
// every declaration and cast below is checked
#pragma cccc checked end
```

```c
void g(void) {
    _Checked {
        int * [[cccc::array, cccc::count(n)]] a = ...;
        _Unchecked { int *raw = (int *)a; }
    }
}
```

| Form | Scope | Notes |
|---|---|---|
| `[[cccc::checked]]` / `[[cccc::unchecked]]` | A function body or a `{ }` block | Also `__attribute__((checked))`, `__checked__`, `@checked` |
| `#pragma cccc checked begin` / `end` (and `unchecked`) | From `begin` to `end` | A positional directive; `end` must name the same keyword as its `begin`, and an unclosed `begin` or stray `end` is an error |
| `_Checked { }` / `_Unchecked { }` | A block, as a statement | Checked C spelling; recognised by position, so the names are not reserved. Not valid as a declarator suffix (`void f(void) _Checked { }`): use the attribute |

Regions nest to any depth and the innermost wins, across all spellings. An attribute or keyword region overrides a surrounding pragma region.

## What is banned

| Rule | Example |
|---|---|
| A pointer-typed local, parameter, global, member or function return type with no checked kind; arrays of such pointers count | `int *p;`, `int *a[2];`, `int *f(void);` |
| An array whose outermost type is not `_Checked`/`_Nt_checked`, as a local, global, member (flexible array members too) or compound literal; `char s[] = "hi"` and `Row a[3]` count | `int a[3];`, `(int[2]){1, 2}` |
| An `array`/`ntarray` pointer with no bounds form | `int * [[cccc::array]] a;` |
| A cast to an unchecked pointer type, or one that changes a checked pointer's kind (`array` to `single`) | `(int *)a` |
| A cast from an unchecked pointer to a checked one | `(int * [[cccc::array, cccc::count(n)]])raw` |
| An implicit conversion from an unchecked pointer to a checked one: initialiser, assignment, argument, `return` | `int * [[cccc::single]] p = malloc(4);` |

A null pointer constant (`NULL`, `0`, `nullptr`) converts to any pointer type. The implicit conversion also accepts array decay, `&lvalue` and pointer arithmetic on them: `int * [[cccc::array, cccc::count(4)]] a = arr;`.

A `bounds(unknown)` target accepts any source, and `assume` and `dynamic` casts are the sanctioned way to convert inside a region ([safety-checked-interfaces.md](safety-checked-interfaces.md#bounds-casts)):

```c
[[cccc::checked]] int f(int n) {
    int * [[cccc::array, cccc::count(n)]] p =
        (int * [[cccc::array, cccc::count(n), cccc::assume]])malloc(n * sizeof(int));
    return p[0];
}
```

Not banned: address-of and array decay producing a plain `T *`, function-pointer pointees, and calling a function with an unchecked prototype (libc, apart from the [functions with built-in bounds](safety-checked-interfaces.md#standard-library-calls)), unless you opt in to [`-Wchecked-calls`](#call-boundaries).[^gaps]

## Call boundaries

`-Wchecked-calls` (off by default) reports calls that cross from a checked region into unchecked code:

```c
int  sink(int *p, int n);                    // unchecked prototype
int *make(int n);

[[cccc::checked]] void f(int * [[cccc::array, cccc::count(n)]] a, int n) {
    sink(a, n);   // warning: checked pointer passed to unchecked parameter 1 of 'sink'
    make(n);      // warning: call to 'make' returns an unchecked pointer
}
```

| Report | When |
|---|---|
| Checked pointer passed to a raw parameter | A checked argument reaches an unchecked `T *` parameter, so its bounds are dropped; one warning per argument |
| Call to an unchecked prototype | The callee has an unchecked pointer parameter or return type; skipped for a call that already reported an argument |

These calls are never reported:

| Callee | Why |
|---|---|
| Declared in a header (all of libc) | Same rule as [headers](#headers) |
| Marked `[[cccc::trusted]]` (or `@trusted`) on any declaration | You vouch for the prototype |
| A prototype whose pointers are all checked, including [annotated parameters](safety-checked-interfaces.md#annotated-parameters) | Nothing crosses unchecked |
| Called through a function pointer | The callee is unknown |

```c
[[cccc::trusted]] int sink(int *p, int n);   // calls to sink stay quiet
```

`-Werror=checked-calls` makes the report an error.

## Headers

A region around an `#include` does not apply to the header's declarations, since every libc prototype is unchecked. An array type from a header typedef, such as `jmp_buf`, is also exempt, because `_Checked` cannot be written on a typedef'd array:

```c
#pragma cccc checked begin
#include <stdio.h>          // fine: header declarations are exempt
int main(void) { printf("hi\n"); return 0; }
#pragma cccc checked end
```

## Downgrading to warnings

A violation is an error by default and stops the build at the first one. To list them all and fix them at your own pace, make them warnings:

```
cccc -Wno-error=checked-regions app.c
```

```c
#pragma GCC diagnostic warning "-Wchecked-regions"   // warnings from here on
```

| Setting | Violation is |
|---|---|
| Default, `-Werror` | An error |
| `-Wno-error=checked-regions`, `-Werror -Wno-error=checked-regions` | A warning |
| `#pragma GCC diagnostic warning "-Wchecked-regions"` | A warning in its extent |
| `-Wno-checked-regions`, `#pragma GCC diagnostic ignored` | An error: a violation is never silenced |

Malformed regions (a stray, unclosed or mismatched `#pragma cccc ... end`, or conflicting attributes) stay errors.

## Always on

The diagnostics are not gated by `--checked-pointers`, and they fire under `-c=native`, `-m` and `-c=generated`. The region attributes are stripped from that output.

[^gaps]: Not enforced: a `typedef` of an unchecked pointer type (each use as a local, parameter, global or member is rejected instead); `-Wchecked-calls` does not follow calls through function pointers; global initialisers and comptime builtin arguments are not checked for unchecked-to-checked conversion.

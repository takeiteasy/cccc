# Checked Regions

Say "only checked pointers in here" for a block, a function or a stretch of file, so code can migrate piece by piece.

Inside a checked region an unchecked pointer declaration or an unsafe pointer cast is a compile error.

## Opening a region

```c
[[cccc::checked]] void f(void) {
    int * [[cccc::array, cccc::count(n)]] a = ...;  // fine
    int *p = a;                                     // error: unchecked local

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
| `#pragma cccc checked begin` / `end` (and `unchecked`) | From `begin` to `end` | A positional directive; an unclosed `begin` or stray `end` is an error |
| `_Checked { }` / `_Unchecked { }` | A block, as a statement | Checked C spelling; recognised by position, so the names are not reserved. Not valid as a declarator suffix (`void f(void) _Checked { }`): use the attribute |

Regions nest to any depth and the innermost wins, across all spellings. An attribute or keyword region overrides a surrounding pragma region.

## What is banned

| Rule | Example |
|---|---|
| A pointer-typed local, parameter, global or member with no checked kind | `int *p;` |
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

Not banned: address-of and array decay producing a plain `T *`, function-pointer pointees, and calling a function with an unchecked prototype (all of libc).[^gaps]

## Headers

A region around an `#include` does not apply to the header's declarations, since every libc prototype is unchecked:

```c
#pragma cccc checked begin
#include <stdio.h>          // fine: header declarations are exempt
int main(void) { printf("hi\n"); return 0; }
#pragma cccc checked end
```

## Always on

The diagnostics are not gated by `--checked-pointers`, and they fire under `-c=native`, `-m` and `-c=generated`. The region attributes are stripped from that output.

[^gaps]: Not enforced: call-boundary diagnostics between checked and unchecked code (an annotated prototype's own bounds are still checked at the call); a C23 `auto` local initialised from an unchecked pointer escapes the declaration ban, so give it an explicit type; global initialisers and comptime builtin arguments are not checked for unchecked-to-checked conversion; and a mismatched `checked begin` / `unchecked end` pair is accepted, since `end` restores whatever state preceded its `begin`.

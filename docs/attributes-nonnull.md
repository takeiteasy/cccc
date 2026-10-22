# Nonnull Attributes

`nonnull` and `returns_nonnull`: compile-time checks that a pointer is never null. They have no codegen effect.

| Attribute | Marks |
|---|---|
| `__attribute__((nonnull))`, `[[gnu::nonnull]]` | Every pointer parameter |
| `__attribute__((nonnull(1,3)))` | Parameters by 1-based index |
| `__attribute__((returns_nonnull))`, `[[gnu::returns_nonnull]]` | The return value |

`__has_attribute` returns 1 for `nonnull`, `__nonnull__`, `returns_nonnull` and `__returns_nonnull__`.

## What is diagnosed

At each call, an argument in a nonnull position (or a `return` in a `returns_nonnull` function) is checked:

| Evidence | Warning |
|---|---|
| A constant that folds to zero (`NULL`, `0`, `(void*)0`) | `-Wnonnull` |
| A local pointer that is provably null on every path | `-Wnonnull` |
| A pointer that is null on some paths | `-Wmaybe-nonnull` (opt-in) |
| A call to a function with a provable null-returning path | `-Wmaybe-nonnull`, or `-Wnonnull` when null on every path[^always] |

```c
void handle(int *p) __attribute__((nonnull(1)));
void handle(int *p) { *p = 1; }

int *make(void) __attribute__((returns_nonnull));
int *make(void) { return 0; }   // warns: null returned from function declared with 'returns_nonnull'

int main(void) {
    handle(0);                  // warns: null passed to a parameter marked nonnull (parameter 1)

    int *p = 0;
    handle(p);                  // warns: null value passed to a parameter marked nonnull (parameter 1)
    return 42;
}
```

A pointer whose address is taken is never tracked. `-Wnonnull` is part of `-Wall`; `-Wmaybe-nonnull` is never implied by `-Wall` or `-Wextra` because its false-positive rate is higher. Disable with `-Wno-nonnull` / `-Wno-maybe-nonnull`.

### Maybe-null

```c
int *p = 0;
if (cond) p = &x;
handle(p);   // -Wnonnull: silent; -Wmaybe-nonnull: warns
```

Branches (`if`, ternary, `&&`, `||`), loops and `switch` are walked and their null-states joined at the merge. A loop or switch that is null on every live path now warns under plain `-Wnonnull` too. Constructs the analysis cannot model (`goto` or labels inside, computed goto, Duff's device) fall back to a conservative scheme that can miss a warning but never invent one.

### Across functions

A pass over the whole translation unit flags pointer-returning functions with a provable null-returning path. Only functions with a visible body count; external or declaration-only functions such as `malloc` are never assumed to return null.

```c
int *maybe_null(int cond) { return cond ? &x : 0; }
int *relay(int cond)      { return maybe_null(cond); }   // flagged transitively

void use(void) {
    handle(maybe_null(1));   // -Wmaybe-nonnull
    int *p = relay(1);
    handle(p);               // -Wmaybe-nonnull
}

int *wrap(void) __attribute__((returns_nonnull));
int *wrap(void) { return maybe_null(1); }   // -Wmaybe-nonnull
```

[^always]: A function that returns null on *every* path is reported under `-Wnonnull`'s message, but discovering the fact needs `-Wmaybe-nonnull` to be passed so the interprocedural pass runs. `-Wmaybe-nonnull` alone does not warn on an always-null callee.

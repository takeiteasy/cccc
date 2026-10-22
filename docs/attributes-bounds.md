# Pointer Bounds Attributes

CCCC-specific attributes that declare a pointer's kind and bounds. Runtime checks need `--checked-pointers`; the full behaviour is in the [checked pointer guides](safety-checked-pointers.md).

## Pointer kinds and bounds

These attach **after the `*`**, in the same slot as `const`/`volatile`/`restrict`. In declspec position a C23 attribute would qualify the pointee.

```c
int  * [[cccc::single]]                     p;  // exactly one object
int  * [[cccc::array, cccc::count(n)]]      a;  // n elements from the pointer's value
char * [[cccc::ntarray, cccc::count(n)]]    s;  // like array, plus a zero terminator slot
int  * [[cccc::array, cccc::bounds(lo,hi)]] r;  // explicit absolute range
```

| Attribute | Meaning |
|---|---|
| `single` | One object; pointer arithmetic on it is a compile error |
| `array` | A run of elements; needs a bounds form |
| `ntarray` | `array` plus one terminator slot that must stay zero |
| `count(n)` | `n` elements |
| `byte_count(n)` | `n` bytes |
| `bounds(lo, hi)` | Explicit `[lo, hi)` |

`__attribute__((...))` and `@single`, `@array`, `@count(n)`, ... are equivalent spellings.

| Rule | Error when broken |
|---|---|
| A bounds form needs `array` or `ntarray` | On `single`, or with no kind |
| Bounds may name any in-scope parameter (even a later one), local or global; on a member they may name a sibling field | An enclosing local or a bit-field sibling |
| A bounds expression has no side effects (it is re-evaluated at every access) | Any side effect |

Enforcement is VM-only. `-c=native`, `-m` and `-c=generated` always parse, type-check and strip the attributes; they enforce the checks only where [stated](safety-checked-interfaces.md#native-output).

Checked arrays use keyword suffixes rather than attributes: `int a _Checked[10]`, `char s _Nt_checked[11]` ([safety-checked-types.md](safety-checked-types.md#checked-arrays)).

## Annotated parameters

A parameter annotated with these attributes is a bounds-safe interface: unchecked callers are unaffected, and a call whose argument is declared-checked is verified against the parameter's bounds. See [safety-checked-interfaces.md](safety-checked-interfaces.md#annotated-parameters).

## `assume` / `dynamic`

Bounds casts that move an unchecked pointer into the checked world. They go on a cast's type name:

```c
int *raw = ...;
int * [[cccc::array, cccc::count(n)]] p =
    (int * [[cccc::array, cccc::count(n), cccc::assume]])raw;    // trust the claim
p = (int * [[cccc::array, cccc::count(n), cccc::dynamic]])raw;   // verify it
```

Details and the comparison table: [safety-checked-interfaces.md](safety-checked-interfaces.md#bounds-casts).

## `dynamic_check`

```c
__builtin_cccc_dynamic_check(i < n);
_Dynamic_check(i < n);   // Checked C spelling
```

Traps when the condition is false under `--checked-pointers`, and does nothing otherwise. The condition must be side-effect-free. See [safety-checked-interfaces.md](safety-checked-interfaces.md#dynamic_check).

## `checked` / `unchecked`

Require every pointer declared in a region to be one of the checked kinds:

```c
[[cccc::checked]] void f(void) {
    int * [[cccc::array, cccc::count(n)]] a = ...;   // fine
    int *p = a;                                       // error: unchecked local
    [[cccc::unchecked]] {
        int *raw = (int *)a;                          // escape hatch
    }
}
```

| Form | Scope |
|---|---|
| `[[cccc::checked]]` on a function | The whole body |
| On a `{ ... }` block | That block; nests, innermost wins |
| `#pragma cccc checked begin` / `end` | From the pragma on |

`__attribute__((checked))`, `__checked__` and `@checked` are equivalent (likewise `unchecked`). The diagnostics are always on, not gated by `--checked-pointers`, and fire under `-c=native` too. See [safety-checked-regions.md](safety-checked-regions.md#checked-regions).

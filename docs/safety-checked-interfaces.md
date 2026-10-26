# Bounds-Safe Interfaces and Casts

Check what callers pass in, and move pointers into the checked world.

| Feature | Use it to |
|---|---|
| [Annotated parameters](#annotated-parameters) | Trap a call whose arguments don't satisfy the callee's declared bounds |
| [Checked return values](#checked-return-values) | Trap a `return` that doesn't satisfy the function's declared return bounds |
| [`assume` / `dynamic` casts](#bounds-casts) | Turn an unchecked pointer into a checked one |
| [`dynamic_check`](#dynamic_check) | Assert a condition, trapping when it is false |

Runtime checks need `--checked-pointers` (or `#pragma cccc config(checked_pointers = true)`); it is not part of any `-0`…`-3` preset. Attribute parsing and type rules are always on. See [safety-checked-pointers.md](safety-checked-pointers.md) for the attributes.

## Annotated parameters

A parameter with checked attributes is its own interface. Unchecked callers need no change.

```c
void sink(int * [[cccc::array, cccc::count(n)]] p, int n);

int * [[cccc::array, cccc::count(2)]] small = ...;
sink(small, 8);   // traps: small has 2 elements, the call claims 8
```

A call is checked when both hold:

| Condition | Example that qualifies | Example that is skipped |
|---|---|---|
| The argument is a declared-checked pointer, or a local that [inherits its bounds](safety-checked-propagation.md) on every path | `small`, `q` after `int *q = small;` | `int *raw` (unchecked to unchecked is untouched) |
| Every other parameter the bounds name has a side-effect-free argument | `sink(small, 8)`, `sink(small, len)` | `sink(small, f())`, `sink(small, i++)` |

`[[cccc::single]]` needs no other argument, so it is always checked. A `_Checked` array argument decays to a declared-checked pointer and is covered too.[^proto]

The bounds are resolved where the prototype is declared, so a bound naming an undeclared identifier, or a global declared after the prototype, is an error there. A bound on an unnamed pointer parameter is enforced the same as on a named one:

```c
void sink(int * [[cccc::array, cccc::count(n)]], int n);
```

An annotated prototype is also what `-Wchecked-calls` accepts from a checked region ([safety-checked-regions.md](safety-checked-regions.md#call-boundaries)).

The callee's own body is checked against its declared bounds as usual, so it can trap even when no unchecked caller is involved.

## Checked return values

A checked return type is verified at each `return` whose value roots in a checked pointer, or in a local that inherits its bounds. The bounds may name parameters, and are evaluated with their values at the `return`.

```c
int * [[cccc::array, cccc::count(n)]] pick(int * [[cccc::array, cccc::count(3)]] src, int n) {
    return src;   // traps when n > 3
}
```

A value with no known bounds (`int *raw`) is returned unchecked.[^ret]

### At the call site

A call to a function with a checked return type carries the declared bounds, with the parameters replaced by the arguments of that call. Every consumer that accepts a checked pointer accepts the call:

```c
int * [[cccc::array, cccc::count(n)]] head(int n);

head(2)[3];                                    // traps: 2 elements
int *q = head(2);   q[3];                      // traps: q inherits [head, head + 2)
int * [[cccc::array, cccc::count(8)]] p = head(2);   // traps: 2 elements don't cover 8
sink(head(2), 8);                              // traps against sink's parameter bounds
```

The arguments the bounds name are evaluated once, before the call, so `head(i++)` and a callee that changes a global keep the bounds the callee was given. A callee that reassigns such a parameter is verified against the value it was called with.

A bound can name scalar parameters (integers and pointers). A call whose bounds name any other parameter type is left unchecked.

## Bounds casts

`assume` and `dynamic` go in a cast's type name, with the checked kind and bounds being claimed:

```c
int *raw = ...;
int n = 5;

// assume: trust the claim, no check
int * [[cccc::array, cccc::count(n)]] p =
    (int * [[cccc::array, cccc::count(n), cccc::assume]])raw;

// dynamic: check the claim against the source's own bounds
int * [[cccc::array, cccc::count(k)]] src = ...;
p = (int * [[cccc::array, cccc::count(n), cccc::dynamic]])src;
```

| | `assume` | `dynamic` |
|---|---|---|
| Checks the conversion | No | Yes, against the source's declared bounds |
| Source must be | Anything | A declared-checked pointer with known bounds, or a local that inherits bounds on every path |
| `bounds(unknown)` | Accepted | Rejected |
| Later accesses through the result | Checked | Checked |

Both need a checked kind, and with `array`/`ntarray` a concrete bounds form. They are mutually exclusive, and only valid in a function body.[^cast]

A `dynamic` cast from a source with no known bounds is a compile error in every mode:

```
error: 'dynamic' cast source has no known bounds to verify against --
  use 'assume' to take responsibility for this conversion, or give the
  source a bounds declaration
```

A local whose bounds are known on only some paths (`int *q = raw; if (c) q = src;`) gets its own error, `'dynamic' cast source has known bounds on only some paths`. A cast works anywhere an expression does, including as a call argument or a `return` value.

Inside a [checked region](safety-checked-regions.md) these are the sanctioned way to change a pointer's checked kind, with no `unchecked` wrapper needed.

## dynamic_check

`dynamic_check(cond)` traps when `cond` is false. Without `--checked-pointers` it does nothing.

```c
dynamic_check(i < n);
```

`__builtin_cccc_dynamic_check` and `_Dynamic_check` are accepted spellings. `cond` must be free of side effects.

## Native output

`-c=native` and `-m` enforce the same checks. A failed check prints the VM's banner (without the `PC:` line) and exits 255:

```
$ ./cccc -c=native --checked-pointers -o oob oob.c && ./oob; echo $?

========== CHECKED BOUNDS VIOLATION ==========
Checked-pointer access out of declared bounds
Address:       0x16d63a454
Access size:   4 bytes
Declared bounds: [0x16d63a448, 0x16d63a454)
================================================
255
```

| Output | Behaviour |
|---|---|
| Without the flag | No checks, helpers or temporaries are emitted |
| Comptime code | Runs in the VM, so it is checked there under the flag |
| `-c=generated` | Warns and drops the flag; it emits no user accesses |
| Attributes | Always stripped from `-E`/`-m`/`-c=generated`/`-c=native` output |

Contract errors (arithmetic on a `single` pointer, side effects in a bounds expression, a member bound naming an enclosing local or a bit-field sibling) are reported in every mode.

[^proto]: A prototype and a later definition that declare different bounds for one parameter are accepted without a diagnostic; the first declaration seen is used. Skipped calls (side-effecting arguments, or a propagated argument whose bounds are known on only some paths) are skipped silently.
[^ret]: A block literal's inferred return type isn't verified. A bound on a return type is resolved where the function is declared, so an undeclared name there is an error in every mode.
[^cast]: A bounds cast is verified by the same check as `q = E;`, so its limits are the ones in [safety-checked-propagation.md](safety-checked-propagation.md#assignments-between-checked-pointers).

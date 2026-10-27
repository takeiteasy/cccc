# Bounds-Safe Interfaces and Casts

Check what callers pass in, and move pointers into the checked world.

| Feature | Use it to |
|---|---|
| [Annotated parameters](#annotated-parameters) | Trap a call whose arguments don't satisfy the callee's declared bounds |
| [Standard library calls](#standard-library-calls) | Check `memcpy`, `read`, `snprintf` and friends with no annotation |
| [Checked return values](#checked-return-values) | Trap a `return` that doesn't satisfy the function's declared return bounds |
| [Plain arrays as sources](#plain-arrays-as-sources) | Check a pointer taken from an array of known size |
| [Struct fields](#struct-fields) | Check a pointer stored into a field, including the count written after it |
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
| The argument is a declared-checked pointer, a local that [inherits its bounds](safety-checked-propagation.md) on every path, or a [plain array](#plain-arrays-as-sources) | `small`, `q` after `int *q = small;`, `int buf[2]` | `int *raw` (unchecked to unchecked is untouched) |
| Every other parameter the bounds name has an argument that is side-effect-free or hoistable | `sink(small, 8)`, `sink(small, f())`, `sink(small, i++)` | verified siblings whose bounds name each other |

`[[cccc::single]]` needs no other argument, so it is always checked. A `_Checked` array argument decays to a declared-checked pointer and is covered too.[^proto]

The bounds are resolved where the prototype is declared, so a bound naming an undeclared identifier, or a global declared after the prototype, is an error there. A bound on an unnamed pointer parameter is enforced the same as on a named one:

```c
void sink(int * [[cccc::array, cccc::count(n)]], int n);
```

## Standard library calls

Under `--checked-pointers`, calls to common libc/POSIX functions are checked against the size they are given, with no annotation in the header or the program:

```c
char a[8], b[16];
memcpy(a, b, 16);   // traps: a has 8 bytes
```

| Header | Functions | Bound on the buffer |
|---|---|---|
| `string.h` | `memcpy`, `memmove` (both buffers), `memset` | `n` bytes |
| `stdio.h` | `fread`, `fwrite` | `size * nmemb` bytes |
| | `fgets`, `snprintf`, `vsnprintf` | `n` bytes |
| `unistd.h` | `read`, `write`, `pread`, `pwrite`, `getcwd`, `readlink`, `gethostname` | the length argument |
| `sys/socket.h` | `recv`, `send`, `recvfrom`, `sendto` | the length argument |

A buffer must hold the whole length even when the call would touch less (`read(fd, a, 16)` into `char a[8]` traps although only 4 bytes may arrive). Calls are checked by the [rules above](#annotated-parameters): the argument must be a checked pointer, an inherited local or a [plain array](#plain-arrays-as-sources); an unchecked pointer is left alone.[^libc-null]

The bounds attach to the declaration in a system header (`<...>` include, or a header such as `<sys/socket.h>` includes), so they apply the same under `--use-system-headers` and `--sysroot`. A program's own function of the same name, and a `static` one, get none.

A program that redeclares one of these functions with its own bounds replaces the built-in ones for the parameters it annotates:

```c
ssize_t read(int fd, void *[[cccc::array, cccc::byte_count(n * 2)]] buf, size_t n);
```

## Redeclarations

Every declaration of a function must spell the same bounds. A mismatch is an error in every mode, as is a different pointer kind (`array` against `ntarray`). A declaration that spells no bounds takes them from one that does, so annotating only the definition checks the calls that follow it:

```c
void sink(int *p, int n);                                        // plain prototype
void sink(int *[[cccc::array, cccc::count(n)]] p, int n) { }     // adds the bounds
void sink(int *[[cccc::array, cccc::count(n + 1)]] p, int n);    // error: conflicting bounds
```

Sibling parameters compare by position, so `count(len)` against `count(n)` agrees. The same rules cover a return bound and the parameters of a function-pointer parameter.[^redecl]

An annotated prototype is also what `-Wchecked-calls` accepts from a checked region ([safety-checked-regions.md](safety-checked-regions.md#call-boundaries)).

The callee's own body is checked against its declared bounds as usual, so it can trap even when no unchecked caller is involved. The body is checked against bounds any declaration spells, so annotating the header once is enough. Sibling parameters bind by position, so the definition may rename them:

```c
int sum(int *[[cccc::array, cccc::count(n)]] p, int n);      // header
int  sum(int *q, int m) { return q[m]; }                      // traps: q[m] is out of bounds
```

This covers a return bound and the parameters of a function-pointer parameter too. Built-in libc bounds are not applied to a user's own definition.

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

A bound can name any parameter, including a struct (`l.n`) or a floating one. A call whose bounds are mutually dependent (two checked pointers that each name the other, both with side effects) is left unchecked.

## Plain arrays as sources

A plain array object of known size is a source for a checked pointer, with the bounds `[buf, buf + sizeof buf)`. Assignments, arguments, return values and `dynamic` casts check against it:

```c
int buf[2];
int * [[cccc::array, cccc::count(8)]] p = buf;   // traps: 2 elements
sink(buf, 8);                                    // traps against sink's parameter bounds
```

| Source | Covered |
|---|---|
| A local or global array, a string literal, `buf + 1`, `&buf[i]` | Yes |
| A member array of a struct object (`h.data`, including the last member) | Yes |
| A row of a multidimensional array (`m[i]`) | Yes: the row, or nothing when `i` is outside `m`[^row] |
| The last member through a pointer (`hp->data`), a variable-length array, an array parameter (a pointer) | No[^tail] |
| `int *q = buf;` then `p = q`, `sink(q, n)` or `return q` | Yes: `q` carries `buf`'s extent into checked targets[^plain] |
| `int *q = buf;` followed by `q[i]` | No: derefs through an unchecked local stay unchecked |

## Struct fields

A pointer stored into a checked field is checked against the bounds the field declares, with the sibling fields read from the same object.

| Site | Checked |
|---|---|
| `s.p = src;` | After the store, or after the sibling writes that follow it ([below](#count-written-after-the-pointer)) |
| `{ .p = src, .n = 8 }`, `{ src, 8 }`, `(struct S){ src, 8 }` | After the whole initializer, whatever the member order |
| `struct S g = { 8, buf };` at file or static scope | At compile time ([below](#constant-initializers)) |
| `s.p = raw;` with an unchecked source | Not checked |

### Count written after the pointer

A write to a field the pointer's bounds name, directly after the pointer's store, belongs to the same check:

```c
s.p = small;   // small has 2 elements
s.n = 8;       // traps here, against the final count
```

The check runs after the last such write. The writes are `=`, `+=`, `++` and `--` on a sibling of the same object, and they may follow stores to other checked pointers that share the count. A call, a label, a branch or any other statement ends the run.[^group] A run that ends in bounds passes: `s.p = small; s.n = 8; s.n = 2;`. A local pointer and the local its count names group the same way.

Writes after the run are rechecked ([below](#count-written-later)).

### Count written later

A count written after other statements is checked against the range the pointer was stored from:

```c
s.p = small;   // small has 2 elements
log();
s.n = 8;       // traps here
```

Every later write to a sibling of the same object is rechecked until something could change the pointer: `=`, `+=`, `++` and `--` count, inside `if`, loops and blocks too. A local pointer initialised once with `&s` and used only as `q->n` or `*q` writes to `s`, and so does `int *pn = &s.n; *pn = 8;`.[^track]

Tracking ends at:

| Event | Example |
|---|---|
| A write to the pointer, or to a variable the object expression reads | `s.p = other;`, `a[k].p = small; k++;` |
| A call or write through a pointer, when the object is a global, is reached through a pointer, or has its address taken | `fill(&s);` |
| A label, `case`, `switch` or statement-expression | `again: s.n = 8;` |
| A nested statement containing any of the above | `if (c) { s.p = other; }` |

Not checked: a count written in a function that did not store the pointer, past the events above, or inside a value that is used (`if ((s.n = f()))`) ([#1823](https://todo.sr.ht/~takeiteasy/cccc/1823)).

### Constant initializers

With `--checked-pointers`, a file-scope or `static` initializer that stores an array into a checked pointer is an error when the declared bounds are constants and overrun the array:

```
error: checked initializer needs 32 bytes but the source array has 8 from this offset
```

```c
int buf[4];
int *[[cccc::array, cccc::count(3)]] p = &buf[2];                 // error
int *[[cccc::array, cccc::bounds(buf, buf + 9)]] q = buf;         // error
```

A `bounds(lo, hi)` range is checked when both ends are constant offsets into one array: it must start inside the array and end at or before its end (one element further for `ntarray`). The pointer's own value is not compared with the range.

A count that is not a constant, a null pointer and a bound spanning two objects are not checked.

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
| Source must be | Anything | A declared-checked pointer with known bounds, a plain array of known size, or a local that inherits bounds on every path |
| `bounds(unknown)` | Accepted | Rejected |
| Later accesses through the result | Checked | Checked |

Both need a checked kind, and with `array`/`ntarray` a concrete bounds form. They are mutually exclusive, and only valid in a function body.[^cast]

A `dynamic` cast from a source with no known bounds is a compile error in every mode:

```
error: 'dynamic' cast source has no known bounds to verify against --
  use 'assume' to take responsibility for this conversion, or give the
  source a bounds declaration
```

A local whose bounds are known on only some paths (`int *q = raw; if (c) q = src;`) gets its own error, `'dynamic' cast source has known bounds on only some paths`. A cast works anywhere an expression does, including as a call argument or a `return` value, and its claimed bounds are what the target, parameter or return type is checked against:

```c
int *[[cccc::array, cccc::count(100)]] q = 0;
q = (int *[[cccc::array, cccc::count(5), cccc::assume]])raw;  // traps: 5 < 100
```

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

[^proto]: Skipped calls (a propagated argument whose bounds are known on only some paths) are skipped silently. A side-effecting argument that a bound names is evaluated once, before the call's other arguments.
[^redecl]: The comparison is structural on the resolved bounds, so `count((n))` agrees with `count(n)` but `count(n + 0)` does not.
[^ret]: A block literal's inferred return type isn't verified. A bound on a return type is resolved where the function is declared, so an undeclared name there is an error in every mode.
[^plain]: The extent flows through the local like any [propagated bounds](safety-checked-propagation.md#plain-arrays), but only checked targets (declarations, assignments, arguments, returns and casts) read it. A local that also holds a checked pointer, or a pointer from elsewhere (`if (c) q = malloc(n);`), is not verified against the array.

[^row]: The range is the row itself, so `&m[0][1]` covers three elements and flattening a matrix into one checked pointer (`sink(m[0], 12)`) traps. A runtime row index outside the array gives an empty range.

[^tail]: Through a pointer a trailing array may stand in for a flexible array member of an over-allocated object, so its declared size is not the object's.
[^track]: Rechecks read the source range the pointer store snapshotted, so a narrowing write passes and a widening one traps at the write. An object that is not a private local (a global, `ps->p`, a local whose address is passed on) ends tracking at the first call, since the callee could replace the pointer.
[^group]: A sibling write whose value has side effects, or reads the pointer, ends the run before it, so the pointer is still checked right after its store. The check moves to the end of the run; the source bounds are still read before the pointer store.
[^cast]: A bounds cast is verified by the same check as `q = E;`, so its limits are the ones in [safety-checked-propagation.md](safety-checked-propagation.md#assignments-between-checked-pointers).
[^libc-null]: `snprintf(NULL, 0, ...)` and other null buffers are unchecked. Functions that may touch fewer bytes than their length (`strncpy`'s source, `strncmp`, `memchr`, `memcmp`) have no bounds.

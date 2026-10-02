# Type compatibility

Two C types are *compatible* when the language treats them as the same type
for a given purpose — assigning through a pointer, selecting a `_Generic`
arm, answering `__builtin_types_compatible_p`. ISO C leaves some of the
details implementation-defined, and where it does, **gcc and clang genuinely
disagree**. This guide covers what CCCC computes, and the `--compiler-family`
switch that picks which of the two host families CCCC's front end models.

## The everyday rules

CCCC follows the C notion of compatible types:

- **Top-level `const` / `volatile` / `restrict` are ignored.** `int` and
  `const int` are compatible; `char *` and `char *const` are compatible.
- **Qualifiers below the top level are significant.** `char *` and
  `const char *` are *not* compatible, and neither are `char **` and
  `const char **` — the mismatch is one level down, not at the top.
- **`_Atomic` is significant below the top level.** `_Atomic int *` and
  `int *` are not compatible; nor are `void(_Atomic int)` and `void(int)`.
- **Arrays** are compatible when their element types are compatible and
  either both lengths are unspecified or the two lengths are equal. `int[3]`
  is compatible with `int[3]` and with `int[]`, but not with `int[4]`.
- **Enumerated types** are compatible with their implementation-defined
  underlying integer type (and vice versa), but two separately declared
  `enum` tags are never compatible with each other even at identical
  width and signedness.

`__builtin_types_compatible_p(t1, t2)` reports exactly this relation and
folds to a `1` / `0` constant at compile time. It parses two `type-name`
operands; spell a function or qualified-return type through a `typedef` if
the inline abstract declarator does not parse.

## `_Generic` arm selection

The controlling expression undergoes the lvalue conversion first: its own
top-level `const`, `volatile`, `restrict`, **and `_Atomic`** are dropped
before any arm is considered. An arm is then chosen when its type is
compatible with that converted type.

```c
_Atomic int ai = 0;
_Generic(ai, int: 1, _Atomic int: 2, default: 0);   // -> 1
```

`ai`'s `_Atomic` is stripped by the conversion, so it matches the plain
`int:` arm. An `_Atomic int:` arm and a plain `int:` arm may coexist in one
selection — they are distinct, so this is not a "two compatible types"
error — but an unqualified controlling expression can only ever pick the
plain one.

A top-level-qualified arm (`const int:`) is likewise unreachable from an
unqualified control, for the same reason.

## The `--compiler-family` switch

Three constructs are where gcc and clang disagree outright, all reached only
through `__builtin_types_compatible_p`:

| construct | gcc | clang |
|---|---|---|
| `__builtin_types_compatible_p(_Atomic int, int)` | `1` | `0` |
| an array element's `_Atomic` (`_Atomic int[3]` vs `int[3]`) | `1` | `0` |
| a function type's own return-type `const`/`volatile` (`volatile int(void)` vs `int(void)`) | `1` | `0` |

gcc drops a *top-level* `_Atomic` and a function's own return-type
qualifiers when deciding compatibility; clang keeps them. (Both families
agree everywhere else, including on a function *parameter*'s `_Atomic`,
which is significant, and its `const`, which is not.)

`--compiler-family=FAM` selects the reading:

- **`gcc`** — the default. Today's behaviour; nothing changes for anyone who
  does not pass the flag.
- **`clang`** — the three constructs above answer `0`. Matches a stock
  clang install, so `-c=native` under macOS's default `cc` round-trips
  these shapes with no `CCCC_NATIVE_CC` override.
- **`auto`** — probe the compiler `-c=native` would use (`CCCC_NATIVE_CC`,
  else `cc`) via its predefined macros and pick `gcc` or `clang` to match.
  Falls back to `gcc` with a warning if no compiler is found or its family
  is unrecognized. `auto` spawns one short compiler process at startup, so
  it is opt-in rather than the default.

The policy is a **parse-time** decision: it feeds folded `sizeof`, member
offsets, and `_Static_assert` constants that the VM and both native
backends must all agree on. There is no "VM assumes gcc, native emits
clang" split.

`_Generic` arm selection is **not** affected by `--compiler-family` — its
behaviour is the one both families already agree on.

### Branching in source

The front end defines `__CCCC_COMPILER_FAMILY__` as `0` for the gcc policy
and `1` for clang, so a translation unit can adapt:

```c
#if __CCCC_COMPILER_FAMILY__ == 1
/* clang reading in effect */
#endif
```

## Known residual

For a `_Generic` controlling expression that is a **cast to an `_Atomic`
type** rather than an lvalue — `_Generic((_Atomic int)0, int: ...)` — gcc
still strips the `_Atomic` (so the `int:` arm matches) while clang keeps it
(so it does not). CCCC has no general lvalue predicate and strips
unconditionally, i.e. follows gcc here regardless of `--compiler-family`.
This shape does not arise in practice.

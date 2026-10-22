# Validation, Clamping and Bit Flags

`@validate`, `@clamp` and `@bitflags`.

## Validation and clamping

`@range(min, max)` on an integer, enum or bit-field member, or on an array of
them, sets its inclusive bounds. `@validate` checks them; `@clamp` enforces them.

```c
typedef enum { LOW = 1, MID, HIGH } Level;

@validate @clamp struct Cfg {
    int      port  @range(1, 65535);
    unsigned retry @range(0, MAX_RETRY);
    Level    lvl   @range(LOW, HIGH);
    int      bits : 4 @range(-3, 3);
    short    grid[2][3] @range(0, 100); // every element
    char     name[16];                  // no @range: not checked
};

struct Cfg c = {.port = 70000};
Cfg_validate(&c);   // 0
Cfg_clamp(&c);      // port is now 65535
Cfg_validate(&c);   // 1
```

| Generated | Behaviour |
|---|---|
| `_Bool T_validate(const T *self)` | 1 when every `@range` member is within its bounds, else 0 |
| `void T_clamp(T *self)` | moves a member below `min` up to `min` and one above `max` down to `max` |

`@validate("P")` and `@clamp("P")` publish `P_T_validate` and `P_T_clamp`. The
bounds are integer constant expressions, so enum constants, `sizeof` and casts
work. Members of an anonymous struct are checked with the struct around them. An
unsigned 64-bit member takes bounds up to `ULLONG_MAX`.

A nested struct member (or an array of them) is checked by calling the nested
struct's own `T_validate` / `T_clamp`, found by the struct's tag with the same
prefix, so the nested struct needs the attribute and is declared first:

```c
@validate @clamp struct Inner { int a @range(0, 9); };
@validate @clamp struct Outer {
    struct Inner one;
    struct Inner many[2];    // Outer_validate checks all three
};
```

A nested struct with no `@range` members, and a named union member, is skipped.

A compile error is raised for:

- `@range` that is not on a struct member, does not have exactly two integer
  constants, or has `min` above `max`
- `@range` on a `_Bool`, floating-point, pointer, `char` array or struct member
- a bound the member's type cannot hold (`unsigned char c @range(0, 300)`,
  `unsigned lo : 3 @range(0, 10)`, a negative `min` on an unsigned member)
- `@validate` or `@clamp` on a union or an anonymous union member
- `@validate` or `@clamp` on a struct with a nested struct that has `@range`
  members but no `T_validate` / `T_clamp` published

Only `@validate` and `@clamp` read `@range`; without them it has no effect.

## Bit Flags

`@bitflags` on an enum whose constants are `0`, powers of two, or
combinations of those powers generates flag helpers:

```c
@bitflags
typedef enum { NONE = 0, R = 1, W = 2, X = 4 } Perm;

Perm p = Perm_set(NONE, R);      // R
p = Perm_toggle(p, X);           // R|X
p = Perm_clear(p, R);            // X
Perm_has(p, X);                  // 1

char buf[32];
Perm_format(R | X, buf, sizeof buf);   // "R|X"
```

| Function | Result |
|---|---|
| `T_set(v, f)` / `T_clear(v, f)` / `T_toggle(v, f)` | `v` with the bits of `f` set / cleared / flipped |
| `T_has(v, f)` | Nonzero when every bit of `f` is set in `v` (always true for `f == 0`) |
| `T_format(v, buf, cap)` | Writes the set flags as `R\|W`, in declaration order |

`T_format` returns the length the whole output needs and always NUL-terminates
when `cap > 0`, like `snprintf`; `cap == 0` only measures. Bits with no
constant print as `0x..` after the names (`R|0x40`). A value of `0` prints as
the `0` constant's name, or `0` when there is none. Constants that share a
value print once, under the first name.

A composite constant is an alias for several flags. `T_format` prints
composites with the most bits first (ties in declaration order), so the alias
replaces the names it covers:

```c
@bitflags
typedef enum { R = 1, W = 2, X = 4, RW = R | W, RWX = R | W | X } Perm;

Perm_format(R | W | X, buf, sizeof buf);       // "RWX"
Perm_format(R | W, buf, sizeof buf);           // "RW"
Perm_format(R | X, buf, sizeof buf);           // "R|X"
Perm_format(R | W | 0x40, buf, sizeof buf);    // "RW|0x40"
```

A negative constant is a compile error that names it, as is a constant with a
bit no power-of-two constant names (`@bitflags: constant 'RX' (5) has bits no
single-bit constant names (0x4)`) and a struct or union target. A prefix names the functions
`<prefix>_<Type>_set` and so on: `@bitflags("app")` on `enum Mode` publishes
`app_Mode_set`.

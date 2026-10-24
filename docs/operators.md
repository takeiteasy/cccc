# Operator overloading

`[[cccc::operator(sym)]]` on a function binds a built-in operator to it for struct and union operands. The parser rewrites `a + b` to a call, so the VM and `-c=native` behave the same.

```c
typedef struct { int x, y; } Vec2;

[[cccc::operator(+)]]
Vec2 vec2_add(Vec2 a, Vec2 b) { return (Vec2){a.x + b.x, a.y + b.y}; }

[[cccc::operator(==)]]
bool vec2_eq(const Vec2 *a, const Vec2 *b) { return a->x == b->x && a->y == b->y; }

[[cccc::operator(<=>)]]
int vec2_cmp(const Vec2 *a, const Vec2 *b) { return a->x - b->x; }

Vec2 c = a + b;       // vec2_add(a, b)
bool same = a == b;   // vec2_eq(&a, &b)
bool less = a < b;    // vec2_cmp(&a, &b) < 0
int order = a <=> b;  // vec2_cmp(&a, &b)
```

## Defining in the struct

An operator written in a struct or union body is the same as the free form, named `<T>_operator_<name>`.

```c
typedef struct Vec2 {
    int x, y;
    Vec2 operator+(Vec2 a, Vec2 b) { return (Vec2){a.x + b.x, a.y + b.y}; }
    bool operator==(const Vec2 *a, const Vec2 *b);   // defined later as Vec2_operator_eq
} Vec2;
```

| Symbol | Name | Symbol | Name |
|---|---|---|---|
| `+` | `add` | `==` | `eq` |
| `-` | `sub` | `!=` | `ne` |
| `*` | `mul` | `<` `<=` | `lt` `le` |
| `/` | `div` | `>` `>=` | `gt` `ge` |
| `%` | `mod` | `<=>` | `cmp` |
| `&` | `and` | `<<` | `shl` |
| `\|` | `or` | `>>` | `shr` |
| `^` | `xor` | | |

- Both parameters are explicit, and at least one is `T` or `T *`.
- A struct has one function per symbol. `Vec2 * int` and `int * Vec2` need the free form.
- `static` and `inline` work as in [methods.md](methods.md#defining-in-the-struct).
- A member named `operator` (`int operator;`) stays a member.

## Operators

| Group | Symbols |
|---|---|
| Arithmetic | `+` `-` `*` `/` `%` |
| Bitwise and shift | `&` `\|` `^` `<<` `>>` |
| Comparison | `==` `!=` `<` `<=` `>` `>=` |
| Three-way | `<=>` |

`&&`, `||`, `,`, `=` and the unary operators cannot be overloaded. Compound assignment (`+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`) uses the binary operator and evaluates `a` once.

## Rules

- The function takes exactly two parameters, and at least one operand is a struct or union.
- A parameter of type `T *` receives `&operand`; a `T` parameter receives the value.
- An rvalue operand (`make() == b`, `(a + b) == b`) passed to a `T *` parameter is copied into a temporary first, so writes through the pointer are discarded.
- Operand types match exactly. `Vec2 * int` and `int * Vec2` are two functions.
- A comparison returns an integer or `bool`. Shifts are not comparisons.
- `a != b` calls the `!=` function when one exists, else `!(a == b)` through the `==` function.
- `a < b`, `a <= b`, `a > b` and `a >= b` call their own function when one exists, else the `<=>` function and compare its result with 0. `==` and `!=` never use `<=>`.
- `a <=> b` binds between shift and relational and does not chain (`a <=> b <=> c` is an error). On struct or union operands it calls the `<=>` function; on integers, floats and pointers it is `-1`, `0` or `1`, and a `NaN` operand gives `0`. It is written without a space.
- `@compare` binds its `T_compare` as `<=>` for `(T, T)` (see [derive-equality.md](derive-equality.md#ordering)).
- `@equals` binds its `T_equals` as `==` for `(T, T)` (see [derive-equality.md](derive-equality.md#equality-hashing-and-dumping)).
- The function is declared before the expression that uses it.
- Two functions for the same operator and operand types are an error, including a hand-written `==` on a type with `@equals`.
- `a += b` needs the operator to return the type of `a`.

## Errors

| Case | Diagnostic |
|---|---|
| No function for the operand types | `invalid operands to +` (any arithmetic, bitwise or shift operator) |
| Comparison of structs with no `==` / `!=` / `<` ... function | `invalid operands to ==` |
| Operator not in the table above | `cannot overload '&&'` |
| Comparison returns a struct | `comparison operator function ... must return an integer or bool` |
| Not two parameters | `operator function ... must take two parameters` |
| No struct operand | `operator function ... needs a struct or union operand` |
| In-struct operator without the struct as an operand | `operator '+' in Vec2 needs a Vec2 or Vec2 * operand` |
| A second `<=>` for the same operand types | `operator '<=>' is already defined for these operand types` |
| Two in-struct operators for one symbol | `operator '*' is already defined in this struct` |

Diagnostics for an in-struct operator name the function `<T>_operator_<name>`.

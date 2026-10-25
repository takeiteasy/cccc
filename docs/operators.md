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
| `^` | `xor` | `[]` | `index` |

Unary forms (one parameter): `-` `neg`, `+` `pos`, `!` `not`, `~` `compl`.

- All parameters are explicit, and at least one is `T` or `T *`.
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
| Subscript | `[]` |
| Unary | `-` `+` `~` `!` (and `++` `--` through `+` / `-`) |

`&&`, `||`, `,`, `=`, `*` and `&` as unary operators, and `->` cannot be overloaded. Compound assignment (`+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`) uses the binary operator and evaluates `a` once.

## Subscript

`[[cccc::operator([])]]` binds `a[i]` for a struct or union `a`. The function returns a pointer, and the parser dereferences it, so `a[i]` is an lvalue.

```c
typedef struct { int data[4]; } Buf;

[[cccc::operator([])]]
int *buf_at(Buf *b, size_t i) { return &b->data[i]; }

buf[1] = 40;         // *buf_at(&buf, 1) = 40
buf[1] += 2;
int *p = &buf[3];
```

- The first parameter is `T` or `T *`, and the function returns a pointer to an object (not `void *`).
- The index converts like a function argument, so `buf[1]` and `buf[n]` work with a `size_t` parameter.
- A type has one `[]` function. A second one is an error whatever its index type.
- In a struct body it is `int *operator[](Buf *b, size_t i)`, named `Buf_operator_index`.
- A pointer or array of structs (`p[i]`) indexes as usual.

## Unary

A one-parameter function binds the unary form. `-` and `+` serve both forms, keyed by parameter count and operand type.

```c
[[cccc::operator(-)]]
Vec2 vec2_neg(Vec2 a) { return (Vec2){-a.x, -a.y}; }

[[cccc::operator(!)]]
bool vec2_is_zero(const Vec2 *a) { return a->x == 0 && a->y == 0; }

Vec2 n = -a;          // vec2_neg(a)
bool zero = !a;       // vec2_is_zero(&a)
```

- `-` and `+` take one or two parameters; `~` and `!` take one; every other symbol takes two.
- The parameter is `T` or `T *`, received as in [Rules](#rules). `!` returns an integer, float or pointer.
- A struct or union operand with no matching function is an error (`invalid operand to unary -`).
- `++a` and `--a` are `a = a + 1` and `a = a - 1`; `a++` and `a--` do the same and yield the old value. They need the `T + int` / `T - int` function exactly (the `1` is an `int`, so `T + long` is not found). `a` is evaluated once, and `a++` is an rvalue.
- In a struct body a unary and a binary `-` coexist: `Vec2 operator-(Vec2 a)` and `Vec2 operator-(Vec2 a, Vec2 b)`.

## Rules

- A binary function takes two parameters, and at least one operand is a struct or union.
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
| Pointer operand to `* / % & | ^ << >>` or unary `+ - ~` | `invalid operands to *`, `invalid operand to unary ~` |
| Floating-point operand to `% & | ^ << >>` or unary `~`, or `_Complex` operand to `% & | ^ << >>` | `invalid operands to %` |
| Comparison of structs with no `==` / `!=` / `<` ... function | `invalid operands to ==` |
| Operator not in the table above | `cannot overload '&&'` |
| `[]` on a struct with no `[]` function | `invalid operands to []` |
| `[]` function not returning a pointer to an object | `subscript operator function ... must return a pointer to an object` |
| `[]` function without a struct or union first parameter | `subscript operator function ... needs a struct or union first operand` |
| A second `[]` for the same struct | `operator '[]' is already defined for these operand types` |
| Comparison returns a struct | `comparison operator function ... must return an integer or bool` |
| Not two parameters | `operator function ... must take two parameters` |
| `~` / `!` without one parameter | `operator function ... must take one parameter` |
| `-` / `+` with three parameters | `operator function ... must take one or two parameters` |
| Unary operator on a struct with no function | `invalid operand to unary -` |
| `!` function not returning a scalar | `operator function ... must return a scalar` |
| `a--` with no `T - int` function | `invalid operands to -` |
| No struct operand | `operator function ... needs a struct or union operand` |
| In-struct operator without the struct as an operand | `operator '+' in Vec2 needs a Vec2 or Vec2 * operand` |
| A second `<=>` for the same operand types | `operator '<=>' is already defined for these operand types` |
| Two in-struct operators for one symbol | `operator '*' is already defined in this struct` |

Diagnostics for an in-struct operator name the function `<T>_operator_<name>`.

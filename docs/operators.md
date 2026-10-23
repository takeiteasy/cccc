# Operator overloading

`[[cccc::operator(sym)]]` on a function binds a built-in operator to it for struct and union operands. The parser rewrites `a + b` to a call, so the VM and `-c=native` behave the same.

```c
typedef struct { int x, y; } Vec2;

[[cccc::operator(+)]]
Vec2 vec2_add(Vec2 a, Vec2 b) { return (Vec2){a.x + b.x, a.y + b.y}; }

[[cccc::operator(==)]]
bool vec2_eq(const Vec2 *a, const Vec2 *b) { return a->x == b->x && a->y == b->y; }

Vec2 c = a + b;       // vec2_add(a, b)
bool same = a == b;   // vec2_eq(&a, &b)
```

## Operators

| Group | Symbols |
|---|---|
| Arithmetic | `+` `-` `*` `/` `%` |
| Comparison | `==` `!=` `<` `<=` `>` `>=` |

`&&`, `||`, `,`, `=` and the unary operators cannot be overloaded. Compound assignment (`a += b`) uses the binary operator and evaluates `a` once.

## Rules

- The function takes exactly two parameters, and at least one operand is a struct or union.
- A parameter of type `T *` receives `&operand`; a `T` parameter receives the value.
- Operand types match exactly. `Vec2 * int` and `int * Vec2` are two functions.
- A comparison returns an integer or `bool`.
- The function is declared before the expression that uses it.
- Two functions for the same operator and operand types are an error.
- `a += b` needs the operator to return the type of `a`.

## Errors

| Case | Diagnostic |
|---|---|
| No function for the operand types | `invalid operands to +` |
| Operator not in the table above | `cannot overload '&&'` |
| Comparison returns a struct | `comparison operator function ... must return an integer or bool` |
| Not two parameters | `operator function ... must take two parameters` |
| No struct operand | `operator function ... needs a struct or union operand` |

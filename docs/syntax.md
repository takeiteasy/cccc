# CCCC syntax extensions

Syntax that standard C does not have. Each section is a summary with a link to the full guide.

| Syntax | What it does | Guide |
|---|---|---|
| `@name(args)` | shorthand for `[[cccc::name]]`, `[[name]]` or `__attribute__` | [attributes-syntax.md](attributes-syntax.md) |
| `T f(T *self) {...}` in a struct body | method, called as `s.f()` | [methods.md](methods.md) |
| `T operator+(T a, T b)` in a struct body | operator overload | [operators.md](operators.md) |
| `a <=> b` | three-way comparison | [operators.md](operators.md) |
| `^^name` | compile-time `Type *` / `Obj *` for a name | [macros-reflection.md](macros-reflection.md) |
| `` `return ${x} + 1;` `` | quasi-quote with `${...}` splices | [macros-quoting.md](macros-quoting.md) |
| `@default(expr)` | member value used by a derived constructor, or the value of an omitted argument | [derive.md](derive.md) |
| `f(.name = v)` | keyed argument, matched to the parameter by name | [derive.md](derive.md#parameter-defaults) |

`$` is an ordinary identifier character (`int $x;`) except inside quasi-quote templates.

## Attributes with `@`

`@name` and `@name(args)` go anywhere an attribute can. The parser picks the first match: a CCCC attribute, a C23 one, a custom comptime handler, then a GNU attribute.

```c
@comptime
Node *answer(void) { return MakeIntLiteral(42); }

struct @packed point { char x; int y; };
```

## Methods

A function written in a struct body is a method. It is an ordinary function named `<T>_<f>` and adds no member, so the layout is unchanged.

```c
typedef struct Vec2 {
    float x, y;
    float len(Vec2 *self) { return sqrtf(self->x * self->x + self->y * self->y); }
} Vec2;

Vec2 v = {3, 4};
float n = v.len();    // Vec2_len(&v)
```

`[[cccc::method(Vec2)]]` on a free function does the same for a struct you do not own.

## Operators

An `operator` function in the struct body binds that operator for the struct. The free form is `[[cccc::operator(+)]]`.

```c
typedef struct Vec2 {
    int x, y;
    Vec2 operator+(Vec2 a, Vec2 b) { return (Vec2){a.x + b.x, a.y + b.y}; }
    int  operator<=>(const Vec2 *a, const Vec2 *b) { return a->x - b->x; }
} Vec2;

Vec2 c = a + b;       // Vec2_operator_add(a, b)
bool less = a < b;    // Vec2_operator_cmp(&a, &b) < 0
```

## Three-way comparison

`a <=> b` gives -1, 0 or 1. On numbers and pointers it needs no function; a NaN operand gives 0. On structs it calls the bound `<=>` function. `<`, `<=`, `>` and `>=` use that same function when they have none of their own.

```c
int order = 3 <=> 5;  // -1
```

## Reflection

`^^name` gives the `Type *` for a typedef or tag, or the `Obj *` for a variable or function. It resolves at parse time, and an unknown name is a compile error.

```c
Type *t = ^^Point;
Obj  *o = ^^counter;
```

## Quasi-quotes

A backtick template is C source that becomes a `Node *`. `${expr}` splices a `Node *` into it; `^^name` reflects inside it.

```c
@comptime
Node *add_one(Node *x) { return `return ${x} + 1;`; }
```

## Member defaults

`@default(expr)` on a member removes it from the `@ctor` constructor's parameters and assigns `expr` instead. A member initializer, `T m = expr;`, is the same thing written in C++ style.

```c
@ctor struct Server {
    int   port  @default(8080);
    char *label = "untitled"; // same as @default("untitled")
};

struct Server s = Server_new();
```

On a function parameter, `@default(expr)` is the value of an omitted argument, and `.name = value` gives an argument by name (`area(.h = 10)` for `int area(int w @default(80), int h @default(24));`); see [derive.md](derive.md#parameter-defaults).

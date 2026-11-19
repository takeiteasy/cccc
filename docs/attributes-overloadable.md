# Function Overloading

`__attribute__((overloadable))` lets functions share a name and differ by parameter types, as in Clang. The call site picks the function.

```c
float  __attribute__((overloadable)) twice(float x)  { return x * 2; }
double __attribute__((overloadable)) twice(double x) { return x * 2; }
int    __attribute__((overloadable)) twice(int x)    { return x * 2; }

int main(void) {
    return twice(21);   // twice(int)
}
```

| Spelling | Form |
|---|---|
| GNU | `__attribute__((overloadable))`, `__attribute__((__overloadable__))` |
| C23 | `[[clang::overloadable]]` |
| Shorthand | `@overloadable` |

`__has_attribute(overloadable)`, `__has_c_attribute(clang::overloadable)` and `__has_extension(attribute_overloadable)` are 1. `__has_extension(overloadable_unmarked)` is 1 too.

## Declaring a set

Every declaration of the name carries the attribute, with one exception: at most one function may leave it off and keep its plain C name.

| Rule | Example |
|---|---|
| A declaration of a marked name is marked | `int f(int) __attribute__((overloadable)); float f(float);` is an error unless `f(float)` is the one unmarked function |
| A redeclaration agrees with the declaration | `int g(int) __attribute__((overloadable)); int g(int) {}` is an error |
| A marked function has a prototype | `int h() __attribute__((overloadable));` is an error before C23 |
| A marked function is at file scope | A block-scope declaration is an error |
| `(...)` alone is allowed | `void honeypot(...);` with `overloadable` and `unavailable` |

```c
int f(int x) { return 1; }                                    // keeps the name `f`
int __attribute__((overloadable)) f(double x) { return 2; }   // `_Z1fd`
```

## Choosing a function

The arguments rank each candidate parameter by parameter, as in C++, with Clang's C rules. The best candidate beats every other in at least one argument and loses to none. If none does, the call is ambiguous.

| Rank, best first | Conversions |
|---|---|
| Exact | Same type |
| Qualification | `int *` to `const int *` |
| Promotion | `char`, `short`, `_Bool` to `int`; an enum to its own `int` or `unsigned`; `float` to `double`; `float` and `double` to `long double` |
| Conversion | Any other arithmetic conversion; `T *` to `void *` and back; `0` to a pointer |
| Incompatible pointer | `char *` to `int *`, only when no other candidate matches |
| Ellipsis | An argument matched by `...` |

```c
void __attribute__((overloadable)) f(int);
void __attribute__((overloadable)) f(long);
void __attribute__((overloadable)) f(double);

short s;
f(s);        // f(int): promotion beats conversion
f(1.0f);     // f(double)
```

A call with no viable candidate is `no matching function for call to 'f'`, and one with several best candidates is `call to 'f' is ambiguous`; both list the candidates with their parameter names and defaults. A string literal ranks as `char *`.

## Defaults and keyed arguments

A parameter with `@default` is optional, and `.name = value` gives an argument by parameter name, as for any function ([derive.md](derive.md#parameter-defaults)). Both take part in choosing the function.

```c
int __attribute__((overloadable)) area(int w, int h @default(24));
int __attribute__((overloadable)) area(double r);

area(5);            // area(int, int), h = 24
area(.h = 2, .w = 5);
area(2.0);          // area(double)
```

| Rule | Example |
|---|---|
| A function is a candidate when every parameter has an argument or a default | `area()` matches neither function above |
| A key matches parameters by name; a function without that name is not a candidate | `area(.r = 1.0)` is `area(double)` |
| A key cannot name a parameter that already has a positional argument | `area(1, .w = 2)` has no candidate |
| An omitted default has no rank, so only the arguments given are compared | `f(1)` with `f(int)` and `f(int, int b @default(0))` is ambiguous |
| Positional arguments come first, then keyed ones | `area(.w = 1, 2)` is an error |

The unmarked function of a name takes defaults and keys too. An argument is checked against its parameter once the function is chosen.

## Taking the address

A function name that stands for several overloads has no type of its own; the type it is converted to picks the one function whose type is that function pointer's.

```c
int __attribute__((overloadable)) hop(int x)    { return 1; }
int __attribute__((overloadable)) hop(double x) { return 2; }

int (*p)(int)    = hop;
int (*q)(double) = &hop;
int (*r)(double) = (int (*)(double))hop;
```

| Where | Example |
|---|---|
| Initializer, at file or block scope, in a struct or array | `int (*t[])(double) = {hop};` |
| Assignment | `p = hop;` |
| Cast to a function pointer | `(int (*)(double))hop` |
| Argument of a function that is not variadic | `take(hop)` |
| Argument of an overloaded function | `via(hop)`, ranked as an exact match when one overload fits that parameter |
| `return` | `return hop;` |

Exactly one overload must have the pointed-to type. No conversion applies, so `int (*)(long)` matches neither `hop(int)` nor `hop(double)`, and an error lists the candidates. Naming a set anywhere else is an error: `1 ? hop : hop`, `p == hop`, `sizeof hop`, `_Generic(hop, ...)`, `typeof(hop)`, `(void *)hop` and a variadic argument. An `unavailable` overload cannot have its address taken.

## Methods and operators

Overloadable functions can share a method name or an operator. The call picks among them as it does for a function call.

```c
typedef struct { int x, y; } Vec;

[[cccc::method(Vec, scale)]]
__attribute__((overloadable))
Vec vec_scale_int(Vec *self, int k);

[[cccc::method(Vec, scale)]]
__attribute__((overloadable))
Vec vec_scale_dbl(Vec *self, double k);

[[cccc::operator(*)]]
__attribute__((overloadable))
Vec vec_mul_dbl(Vec a, double k);

v.scale(2);        // vec_scale_int
Vec.scale(&v, 2.0);  // vec_scale_dbl
v * 2.5;           // vec_mul_dbl
```

| Rule | Behavior |
|---|---|
| Who may share a method name | Overloadable functions only; two plain functions for one (`T`, name) are an error |
| Static and instance | One name is all static or all instance: `method 'f' cannot be both static and an instance method` |
| `s.f(args)` | The receiver is not ranked; the arguments after it are |
| `T.f(args)` | Every parameter is ranked, the receiver of an instance method included |
| Operators | An exact match on the operand types wins, then an operator template, then the overloadable functions are ranked on the operands |
| `a != b`, `a < b` | An exact function for the operator, `==` or `<=>` is tried before any ranked one |
| `a[i]` | An overloadable `[]` function takes the index types it names exactly, then ranked |
| Plain function of the set | The unmarked function of a name keeps its plain name and cannot be a method of the same name |
| In a struct body | `@overloadable` on an in-struct method or operator ([methods.md](methods.md#defining-in-the-struct)) |

A call with no match is `no matching function for call to 'f'`, and one with several best candidates is `call to 'f' is ambiguous`, as for a function. For an operator the candidates are named `operator*`; an operator with no matching function is still `invalid operands to *`.

## Names in the output

Each marked function is named by its Itanium C++ mangling, the one Clang uses, so `twice(double)` is `_Z5twiced`. `-m` and `-c=native` print and link that name, and a Clang-built object that defines the same overloads links against it. The unmarked function keeps its plain name. `__func__` and diagnostics give the name as written.

A parameter type that has no name to mangle (an anonymous struct) is an error; give the struct a tag or a typedef name.

## Unavailable functions

`__attribute__((unavailable))` or `unavailable("message")` makes a call to the function an error, unless the call is in a statically dead branch. An unavailable `(...)` overload turns a call with no better match into that error, which is how `<tgmath.h>` rejects a non-arithmetic argument.

## Limitations

| Limit | Behavior | Ticket |
|---|---|---|
| `char` and `signed char` | One type in CCCC, so they cannot be overloads of each other | [#2089](https://todo.sr.ht/~takeiteasy/cccc/2089) |
| `__int128` and `_BitInt(128)` | One type in CCCC; a 128-bit parameter is an error | [#2089](https://todo.sr.ht/~takeiteasy/cccc/2089) |
| A function template with the set's name | Rejected | [#2087](https://todo.sr.ht/~takeiteasy/cccc/2087) |
| Block-scope declarations | Rejected | [#2090](https://todo.sr.ht/~takeiteasy/cccc/2090) |
| Ranking | A few rare calls rank differently from Clang | [#2091](https://todo.sr.ht/~takeiteasy/cccc/2091) |
| `enable_if`, `diagnose_if`, `pass_object_size` | Not recognized | |

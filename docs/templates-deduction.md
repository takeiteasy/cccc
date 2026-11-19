# Template Argument Deduction

A function template called by name deduces its arguments from the types of the call's arguments. `@inst` is only needed to override or to name the function without calling it.

```c
@template(T) T maxof(T a, T b) { return a > b ? a : b; }

int m = maxof(40, 42);          // maxof_int
double d = maxof(1.5, 0.5);     // maxof_double
```

A deduced call and `@inst(int) maxof(...)` name the same instance. A local variable or function called `maxof` hides the template, as it hides any other name.

## What the argument's type is

| Argument | Deduces |
|---|---|
| `40`, `'a'`, `1.5` | The literal's own type: `int`, `int`, `double` |
| `int a[3]` for `T *` | `int`: the array decays to a pointer |
| `"hi"` for `T *` | `char` |
| A function name for `T (*f)(T)` | The function's type, as a pointer |
| `const int c` for `T` | `int`: top-level qualifiers are dropped |
| `const int *` for `T *` | `const int` |
| `int *` for `const T *` | `int`: the call then converts to `const int *` |

Two arguments that bind one parameter must agree. `maxof(1, 2.0)` is an error; there is no conversion between `int` and `double` to choose from.

## Patterns

A parameter is matched against the argument when its type names a template parameter. Other parameters take the usual conversions.

| Parameter | Argument |
|---|---|
| `T`, `T *`, `const T *`, `T **` | Any type of that shape |
| `T (*f)(T)` | A function pointer; the return and parameter types bind `T` |
| `@inst(T) Vec` | An instance of `Vec`; its argument binds `T` |
| `@inst(T, N) Arr` | An instance of `Arr`; its arguments bind `T` and the integer `N` |
| `T (*p)[N]` | A pointer to an array; its length binds `N` |
| `size_t n` | Not matched: any integer converts |

```c
@template(T) struct Vec { T x, y; };
@template(T) T sum(@inst(T) Vec v) { return v.x + v.y; }

@inst(int) Vec v = {20, 22};
int s = sum(v);                 // T is int
```

## Naming some arguments

`@inst` before a call names the leading arguments and leaves the rest to deduction. A parameter named this way takes conversions, so `T` is not compared with the arguments.

```c
@template(T, U) U widen(T a, U b) { return (U)a + b; }

double d = @inst(int) widen(1, 2.0);    // T is int, U is double
double e = @inst(double) maxof(1, 2.5); // T is double; 1 converts
```

## Parameter packs

A function pack, `Ts... xs`, takes one type per argument that the fixed parameters leave over.

```c
@template(Ts...) int sum(Ts... xs) { return (xs + ... + 0); }
@template(T, Ts...) int lead(T a, Ts... rest) { return a * 100 + (rest + ... + 0); }
@template(Ts...) int tail(Ts... xs, int last) { return last + (xs + ... + 0); }

sum(40, 2L);        // sum@(int, long)
sum();              // sum@(): an empty pack
lead(5, 1, 2);      // T is int, Ts is (int, int)
tail(1, 2, 3);      // Ts is (int, int); 3 is `last`
```

The pack may come after fixed parameters or before them. An element may be a pattern, as in `Ts *... ps`: each argument must match it, and the pack holds what `Ts` matched.

```c
@template(Ts...) int derefs(Ts *... ps) { return (*ps + ... + 0); }

int a = 5; char b = 6;
derefs(&a, &b);     // Ts is (int, char)
```

The return type takes no part in deduction, so it may name the pack: `@template(Ts...) Ts...[0] first(Ts... xs)` called as `first(42, 2.0)` returns an `int`.

Each argument is typed as for any other parameter (arrays decay, top-level qualifiers drop). `@inst` names the leading template arguments, the first pack elements included, and the call deduces the rest. A named element takes conversions, as a named parameter does.

```c
sum(5, 2.0);                // sum@(int, double)
@inst(double) sum(5, 2);    // sum@(double, int): 5 converts
@inst(int) lead(5, 1, 2);   // T is int, Ts is (int, int)
```

A template with no pack among its function parameters, or one before C `...`, takes the whole list: `@inst(int, char) f()`.

## Defaults

A deduced value wins over a default. A default fills a parameter the arguments do not give.

```c
@template(T, U = T) T add_to(T a, U b) { return a + (T)b; }

add_to(40, 2);      // U is int, deduced
add_to(40, 2.5);    // U is double, deduced: the deduced value beats the default
```

A defaulted parameter that is skipped while a later one is deduced is an error: `template parameter 'U' has a default but a later parameter is deduced; give it in '@inst(...)'`.

## Limits

| Not deduced | Result |
|---|---|
| A parameter that appears only in the return type | `template parameter 'U' is not deduced by the arguments`; name it with `@inst` |
| An array length through `T a[N]` | The parameter is `T *`, so `N` is not deduced; write `T (*p)[N]` or an `Arr` instance |
| A float or pointer constant | Name it with `@inst` |
| A pack before C `...` (`Ts... xs, ...`) | `cannot deduce the pack of 'f' before '...'`; use `@inst` |
| A pack that is not a function parameter | `template parameter 'Ts' is not deduced by the arguments`; use `@inst` |
| A defaulted parameter before a deduced pack | `template parameter 'U' has a default but a later parameter is deduced`; give it in `@inst` |
| A keyed (`.b = 2`) argument or a splice placeholder | Positional arguments only; use `@inst` |
| A shape that does not fit | `cannot deduce the arguments of 'f': argument 1 ('int') does not match its parameter` |

Two function templates cannot share a name. A template that shares its name with an [overload set](attributes-overloadable.md#templates) is ranked with the set's functions, so a call takes the best of them.

An error inside the instance carries the usual `in instantiation of` note at the call.

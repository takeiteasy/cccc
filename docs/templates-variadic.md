# Template Parameter Packs

A template whose last parameter ends in `...` takes any number of arguments there. The body expands the pack with `...` in the same way as C++ variadic templates.

```c
@template(Ts...) struct Tuple { Ts... v; };           // struct Tuple__int__char { int v0; char v1; };
@template(Ts...) int sum(Ts... xs) { return (xs + ... + 0); }

@inst(int, char) Tuple t = {40, 2};
int s = sum@(int, long)(40, 2);                        // sum__int__long
```

## Declaring a pack

| Parameter | Takes | Example |
|---|---|---|
| `Ts...` | Any number of types | `@template(Ts...)`, `@template(T, Ts...)` |
| `int... Ns` | Any number of integer constants | `@template(T, int... Ns)` |

The pack is the last parameter, and a template has one. A pack takes no default, and a template with a pack has no defaulted parameters. A constant pack has an integer type (`bool`, `char`, an enum, `_BitInt(N)` up to 64 bits); float and pointer packs are an error.

```c
@inst(int, 2, 3, 4) Nd nd;      // @template(T, int... Ns) struct Nd {...}
@inst Tuple e;                  // an empty pack: Tuple
```

An instance takes at least the parameters before the pack: `template 'Wrap' takes at least 1 argument, 0 given`. The number of arguments has no upper limit.

## Expanding a pack

| Spelling | Meaning | Example |
|---|---|---|
| `sizeof...(Ts)` | The number of elements | `sizeof...(Ts)` is `3` for `int, char, long` |
| `Ts...[k]`, `xs...[k]` | Element `k`, a constant expression | `xs...[1]` is `xs1` |
| `Pattern...` | The pattern once per element, comma separated | `f(xs * 2 ...)` is `f(xs0 * 2, xs1 * 2)` |
| `Pattern... name` | A declaration per element, named `name0`, `name1`, ... | `Ts... xs` is `int xs0, char xs1` |
| `(xs op ...)` and the other folds | The elements joined by `op` | `(xs + ...)` is `(xs0 + (xs1 + xs2))` |

A pattern is the list item before `...`: everything since the last comma, semicolon or opening bracket. A bare pack name is an error: `parameter pack 'Ts' must be expanded with '...'`.

### Members and parameters

`Ts... name` declares one name per element, in a struct, union, function parameter list or function body.

```c
@template(Ts...) struct Ptrs { Ts *... p; };          // int *p0; char *p1;
@template(T, Ts...) int lead(T a, Ts... rest) {       // int lead(int a, int rest0, int rest1)
    return a * 100 + (rest + ... + 0);
}
```

A declared pack has the same length as the template's pack, and expands the same way in the body (`rest...`, `rest...[0]`, `sizeof...(rest)`).

### Patterns

```c
@template(Ts...) int doubled(Ts... xs) { return sum@(Ts...)(xs * 2 ...); }
@template(Ts...) int sizes(void)       { int a[] = {sizeof(Ts)...}; return a[0]; }
@template(T, Ts...) struct Wrap        { @inst(T, Ts...) Tuple t; };   // forwarding
```

A pattern holds one pack expansion and may not contain another.

### Folds

| Form | Expands to |
|---|---|
| `(... op xs)` | `((x0 op x1) op x2)` |
| `(xs op ...)` | `(x0 op (x1 op x2))` |
| `(init op ... op xs)` | `(((init op x0) op x1) op x2)` |
| `(xs op ... op init)` | `(x0 op (x1 op (x2 op init)))` |

`op` is a binary operator, including `&&`, `||`, `,` and the assignments. Only one side of a binary fold holds a pack.

### Empty packs

An empty pack removes its list item and one adjacent comma, so `f(int a, Ts... rest)` becomes `f(int a)`. A member declaration disappears with its semicolon. An empty unary fold is `1` for `&&`, `0` for `||` and `(void)0` for `,`; any other operator needs the binary form: `a fold over '+' of an empty pack needs an initial value`.

### With C varargs

A C `...` after a pack stays a C `...`: `int log(int n, Ts... xs, ...)`.

## Specializing

An exact `@specialize` names the whole argument list, including an empty one.

```c
@template(Ts...) struct Tuple { Ts... v; };
@specialize() struct Tuple { char none; };             // the empty pack
@specialize(short, short) struct Tuple { int both; };
```

A partial specialization of a variadic template, and a pattern that names an instance of one, are an error.

## Instance names

An instance is `Name__Arg` for each argument, as for any template[^names].

| Instance | Name |
|---|---|
| `@inst(int, char) Tuple` | `Tuple__int__char` |
| `@inst Tuple` (empty pack) | `Tuple` |
| `@inst(int, 2, 3) Nd` | `Nd__int__2__3` |

## Limits

| Not supported | Result |
|---|---|
| Float or pointer constant packs | Compile error |
| A pack parameter in the middle of the list, or two packs | Compile error |
| Defaults in a template with a pack | Compile error |
| Partial specialization of a variadic template | Compile error |
| A pack expansion inside another pack's pattern | Compile error |
| Expanding another instance's members (`t.v0...`) | Compile error |

[^names]: Two argument lists can mangle to one name, for example when an argument's own name contains `__`. The instance clash check compares the argument count and each argument, so such a pair is an error (`cannot name the instance`), not a silent merge.

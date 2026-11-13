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
| `double... Ds` | Any number of floats | `@template(double... Ds)` |
| `int *... Ps`, `int (*...Fs)(int)` | Any number of pointers or functions | `@template(int (*...Fs)(int))` |

The pack is the last parameter, and a template has one. A pack takes no default. A constant pack has any type a single constant parameter has (an integer up to 64 bits, `float`, `double` or a pointer), and each element follows the same rules as that single parameter. A [partial specialization](#partial-specialization)'s pack variable has the kind of the pack it takes: `pack 'Ns' holds integer constants, but 'f' takes floating ones`.

```c
@inst(int, 2, 3, 4) Nd nd;      // @template(T, int... Ns) struct Nd {...}
@inst Tuple e;                  // an empty pack: Tuple
```

An instance takes at least the parameters before the pack that have no default: `template 'Wrap' takes at least 1 argument, 0 given`. The number of arguments has no upper limit.

### Defaults before a pack

Parameters before the pack may have defaults. Arguments fill the parameters left to right, so the pack takes arguments only after every defaulted parameter has one.

```c
@template(T, U = T, Ts...) struct X { T a; U b; Ts... rest; };

@inst(int) X p;                // X__int__int: U is int, the pack is empty
@inst(int, char) X q;          // X__int__char
@inst(int, char, long) X r;    // X__int__char__long: Ts is (long)
```

Every filled default is in the instance name, so `@inst(int) X` and `@inst(int, int) X` are one instance.

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

A pattern may hold expansions and folds of its own. Each `...` expands the packs inside its own pattern, and the outer pattern must name a pack outside them.

```c
@template(Ts...) int cross(Ts... xs) { return add2(mix3(xs, xs...)...); }  // add2(mix3(x0, x0, x1), mix3(x1, x0, x1))
@template(Ts...) int scaled(Ts... xs) { return add2((xs + ... + 0) * xs ...); }
```

`g(h(xs...)...)` is an error: `the pattern before '...' contains no parameter pack`.

### Folds

| Form | Expands to |
|---|---|
| `(... op xs)` | `((x0 op x1) op x2)` |
| `(xs op ...)` | `(x0 op (x1 op x2))` |
| `(init op ... op xs)` | `(((init op x0) op x1) op x2)` |
| `(xs op ... op init)` | `(x0 op (x1 op (x2 op init)))` |

`op` is a binary operator, including `&&`, `||`, `,` and the assignments. Only one side of a binary fold holds a pack.

### Member packs

`Ts... name;` in a struct declares `name0`, `name1`, ... in every instance. `t.name...` expands those members of an instance `t`: in a call, an initializer list, a fold, `sizeof...` and `...[k]`.

```c
@template(Ts...) struct Tuple { Ts... v; };

Tuple@(long, long) t = {4, 2};
int a = add(t.v...);            // add(t.v0, t.v1)
int b = (t.v + ...);            // (t.v0 + t.v1)
int n = sizeof...(t.v);         // 2
int c = t.v...[1];              // t.v1
```

`t` is any expression of the instance type (`o.inner`, `p->`, `(*p)`, `get()`), and it is repeated for each member, so `get().v...` calls `get` once per member. An instance holds the pack its selected specialization declares: `Tuple@(int, char)` with `@specialize(int, Ts...) struct Tuple { int first; }` has no `v`, and `t.v...` is an error: `the pattern before '...' contains no parameter pack`. A bare `t.v` is an error too: `member pack 'v' must be expanded with '...'`.

A pattern may name several instances (`a.v + b.v ...`) when their packs have the same length: `the packs in one pattern have different lengths (2 and 3)`.

A function template's pattern may also hold its own pack: `xs + t.v ...` adds each element of `xs` to the matching member of `t`, in a call, an initializer list or a fold. The two packs must have the same length.

```c
@template(Ts...) int weigh(Tuple@(Ts...) t, Ts... xs) {
    return add2(xs + t.v ...);  // add2(xs0 + t.v0, xs1 + t.v1)
}
```

A struct or union template body expands a member pack in a member declaration, such as an array length. A template pack may share the pattern:

```c
Tuple@(char, int) bytes;

@template(Ts...) struct Sized {
    char b[(sizeof(Ts) + sizeof(bytes.v) + ...)];  // Sized@(char, int): 10 bytes
};
```

A declaration at file scope expands a member pack too, in an array length, an initializer list or a `_Static_assert`:

```c
Tuple@(char, int) bytes;

char flat[(sizeof(bytes.v) + ... + 0)];      // 5 bytes
int  widths[] = {sizeof(bytes.v)...};        // {1, 4}
_Static_assert(sizeof...(bytes.v) == 2, "two members");
```

The object must be declared before the statement that expands it, so a `for (Tuple t = ...; ...; f(t.v...))` header cannot expand its own `t`. The condition of a `do ... while (f(t.v...));` is not expanded.

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

### Partial specialization

A partial specialization names its leading arguments, then a pack variable that takes the rest, written alone as the last item (`Ts...`).

```c
@template(Ts...) struct Tuple { Ts... v; };
@template(T, Ts...)
@specialize(T *, Ts...)
struct Tuple { T *head; Ts... tail; };  // first element a pointer
@template(Ts...)
@specialize(int, Ts...)
struct Tuple { int first; };  // first element int
@template(T)
@specialize(T *)
struct Tuple { T *only; };  // exactly one pointer
@template(T, int... Ms)
@specialize(T, 0, Ms...)
struct Nd { int rest[sizeof...(Ms) + 1]; };
```

An element pattern before the `...` makes every argument of the tail match it, and the pack variable takes what each one matched (`Ts` is `int, char` for `int *, char *`).

```c
@template(Ts...) struct Ptrs { char kind[1]; };
@template(Ts...)
@specialize(Ts *...)
struct Ptrs { Ts... pointee; };  // every argument a pointer
@template(Ts...)
@specialize(Ts **...)
struct Ptrs { Ts... pointee; };  // narrower: every argument a pointer to pointer
@template(T, Ts...)
@specialize(T, Pair@(T, Ts)...)
struct Links { Ts... rest; }; // a leading variable may repeat
```

A pack variable may appear more than once. It then matches only when every use takes the same arguments.

```c
@template(Ts...) struct Pack { Ts... v; };
@template(Ts...) struct Same { char kind; };
@template(Ts...)
@specialize(Pack@(Ts...), Ts...)
struct Same { char kind[2]; };

Same@(Pack@(int, char), int, char) a;   // the partial: both uses are (int, char)
Same@(Pack@(int, char), int, long) b;   // the primary: they differ
```

The body sees the pack variable as a pack: `sizeof...(Ts)`, `Ts...[k]`, `Ts... name` and folds work on the arguments it took. A pattern may also name an instance of a variadic template: `@specialize(Pack@(T, Ts...))`.

| Rule | Detail |
|---|---|
| Length | Without a pack variable a pattern matches that many arguments, defaults filled in; with one, that many or more. `(T, Ts...)` matches one argument or more |
| Selection | The most specialised matching partial wins: `(T, U)` over `(T, U, Ts...)` over `(T, Ts...)`, `(int, Ts...)` over `(T, Ts...)`, and `(Ts *...)` over `(Ts...)`. Partials where none is narrower than all the others are an error. An empty pack matches every element pattern, so the narrowest wins |
| Pack variable | A pack parameter of the `@template`, of the same kind (types or constants) as the pack it takes, written `Ts...` or `<pattern of Ts>...` as the last item of a list; it may stand where the primary has defaulted parameters, and then takes their arguments too (`(int, Ts...)` for `(T, U = T, Us...)`). Used again (`Pack@(Ts...), Ts...`), it stands for one argument list: the same length, and each element the same type or value |
| Element pattern | Type packs only. It names the pack variable, and any other variable in it must also be deduced by a leading item |
| Errors | `a pack in a pattern is written alone as the last item, 'Ts...'`, `the pattern before '...' names no pack variable`, `cannot expand pack 'Ts' into template 'Pair', which is not variadic`, `a partial specialization must be more specific than the primary template` |

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
| A pack parameter in the middle of the list, or two packs | Compile error |

[^names]: Two argument lists can mangle to one name, for example when an argument's own name contains `__`. The instance clash check compares the argument count and each argument, so such a pair is an error (`cannot name the instance`), not a silent merge.

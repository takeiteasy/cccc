# Templates

Generic structs, unions, enums, typedefs and functions: write the definition once with type and constant parameters, instantiate it per argument list.

```c
@template(T) struct Vec { T *data; size_t len, cap; };
@template(T) T max(T a, T b) { return a > b ? a : b; }

int main(void) {
    @inst(int) Vec v = {0};          // struct Vec__int
    return @inst(int) max(40, 42);   // max__int
}
```

## Defining a template

`@template(P, ...)` goes before a file-scope struct, union, enum, typedef or function **definition**. The body is kept as tokens and is not compiled until it is instantiated, so a template that is never used costs nothing and never reaches the output.

| Rule | Detail |
|---|---|
| Parameters | One or more type names (`@template(K, V)`) or constants (`@template(T, int N)`, `@template(double D)`, `@template(int (*F)(int))`), optionally with defaults, and a last [parameter pack](templates-variadic.md) (`Ts...`, `int... Ns`, `double... Ds`) |
| Allowed on | A struct, union, enum, typedef or function definition at file scope |
| Not allowed on | Prototypes, `extern`, a struct or enum that declares variables, a typedef that declares more than one name, anything inside a function |
| Attributes | Other attributes on the declaration stay with it and apply to every instance: `@template(T) @equals struct Cell { T a; T b; };`. A struct, union or enum may also carry them between the keyword and the tag or after the closing brace: `@template(T) struct [[gnu::aligned(16)]] Slot { T v; };` |
| Name | One template per name; a second definition is an error |

## Constant parameters

A parameter written as a type followed by a name is a constant; integers come first, [float and pointer constants](#float-and-pointer-constants) after. The matching `@inst` argument is a constant expression, and the name is a constant of that type inside the body: usable in array sizes, `case` labels and `_Static_assert`, and hidden by a local of the same name.

```c
@template(T, int N) struct Arr { T v[N]; };
@template(T, T N) T add_n(T x) { return x + N; }

@inst(int, 16) Arr a;              // struct Arr__int__16
int r = @inst(long, 40) add_n(2);  // add_n__long__40
```

| Rule | Detail |
|---|---|
| Type | Any integer type up to 64 bits (`bool`, `char`, an enum, `_BitInt(N)`); it may name an earlier type parameter |
| Value | Must fit the type, or the instantiation is an error: `template argument 300 does not fit 'unsigned char'` |
| Same value | `16`, `0x10` and `8 * 2` name one instance |
| Negative | Named with an `n` prefix: `-3` gives `signed_n__n3` |
| Enum or bool | Named by the enumerator (`Pick__GREEN`) or `true`/`false`; a value with no enumerator keeps its number[^enumname] |

### Float and pointer constants

A `float` or `double` parameter takes any real constant expression; a pointer parameter takes a function, the address of an object, or null. Inside the body the name is a `const` object that folds in constant expressions, so a local of the same name hides it.

```c
@template(double D) double scale(double x) { return x * D; }
@template(T, int (*CMP)(T, T)) int before(T a, T b) { return CMP(a, b) < 0; }
@template(const int *P) int first(void) { return *P; }

static int cmp_int(int a, int b) { return a - b; }
static int table[4];

double r = @inst(1.5) scale(2.0);          // scale__1p5
int    s = @inst(int, cmp_int) before(1, 2);  // before__int__cmp_int
int    t = @inst(table) first();           // first__table
```

| Parameter | Argument | Instance name |
|---|---|---|
| `float`, `double` | Any real constant; converted to the parameter type first | Shortest decimal that reads back the same: `1.5` is `1p5`, `-0.25` is `n0p25`, `1e-5` is `1em05`, `-0.0` is `n0` |
| Function pointer | A function, or `&function` | The function's name: `before__int__cmp_int` |
| Object pointer | `&object`, or an array (its first element), with file-scope or `static` storage | The object's name: `first__table` |
| Any pointer | `NULL`, `nullptr` or `0` | `null` |

| Rule | Detail |
|---|---|
| Float values | `inf` and `-inf` are accepted (`inf`, `ninf`); NaN is an error. A finite value that overflows `float` is an error: `template argument 1e+40 does not fit 'float'` |
| Pointer types | The argument must match the parameter: a function for a function pointer, an object for an object pointer, compatible pointee types. `void *` takes any object |
| Not accepted | String literals (pass a named `static const char x[]` as `x`), `&a[2]`, `&s.m`, locals, block-scope statics, compound literals, thread-local objects |
| Constant expressions | A float parameter folds (`_Static_assert(D > 1.0)`, `int a[(int)D]`). A non-null pointer parameter is a valid static initializer (`static int (*f)(int) = F;`) but not an integer constant expression |
| Name clash | An argument spelled like another (`null` as a function name and as null) is an error: `cannot name the instance: 'f__null' is also the name of a different instance` |

## Default arguments

A parameter may end in `= value`, and the instantiation then leaves its argument out. A type parameter takes a type, a constant parameter a constant expression.

```c
@template(T, int N = 8) struct Arr { T v[N]; };
@template(K, V = K) struct Map { K key; V val; };
@template(T = int) struct Id { T v; };
@template(T, T N = 0) T add_n(T x) { return x + N; }
@template(double D = 1.0, double E = D * 2) double span(void) { return E - D; }
@template(int (*F)(int) = NULL) int apply(int x) { return F ? F(x) : x; }

@inst(int) Arr a;            // struct Arr__int__8
@inst(int, 8) Arr b;         // the same type
@inst(int) Map m;            // struct Map__int__int
@inst Id c;                  // every parameter defaulted: struct Id__int
@inst(3.0) span;              // span__3__6
```

| Rule | Detail |
|---|---|
| Order | Defaulted parameters come last, before any [parameter pack](templates-variadic.md#defaults-before-a-pack); a parameter after a default needs a default |
| Value | May name earlier parameters (`T N = 0`, `int N = sizeof(T)`); naming itself or a later parameter is an error |
| Evaluated | Per instance, after the earlier arguments are bound |
| Name | Every argument is in the instance name, so `@inst(int) Arr` and `@inst(int, 8) Arr` are one instance |
| Too few or many | `template 'Arr' takes 1 to 2 arguments, 0 given` |

## Enum and typedef templates

An enum instance is `Name__Arg` and its enumerators are renamed `Name__Arg__ENUMERATOR`, so two instances never collide. A typedef instance is a real file-scope typedef named `Name__Arg`.

```c
@template(T) enum Level : T { LOW, HIGH = LOW + 5 };
@template(T) typedef T *Ptr;
@template(T, int N) typedef T Row[N];
@template(T) typedef struct Link { T val; struct Link *next; } Link;

@inst(char) Level lv = Level__char__HIGH;   // enum Level__char
@inst(int) Ptr p;                           // typedef int *Ptr__int
@inst(int, 4) Row r;                        // typedef int Row__int__4[4]
@inst(int) Link l;                          // struct Link__int, typedef Link__int
```

| Rule | Detail |
|---|---|
| Enum | Needs a tag and a definition; the base type may name a parameter (`: T`) |
| Typedef | Declares one name, which may be parenthesised after a type name (`typedef Count (Fn)(T);` declares `Fn`); a struct, union or enum tag defined inside is omitted or equals that name |
| Function | Needs a body; the name may be parenthesised after a type name (`T (fn)(T x) {...}`), and the return type may be `@inst(T) Vec`, `_Atomic(T)` or a function pointer (`int (*pick(T x))(int) {...}`) |
| Enumerators | Renamed in the whole body, so `HIGH = LOW + 5` still resolves |

## Instantiating

`@inst(Type, ...) Name` names an instance, with the arguments before the name as `@template(T)` has them. It works wherever a type or an expression is allowed, at file scope or inside a function: declarations, `sizeof`, casts, compound literals, parameter lists and other templates.

```c
@inst(int, float) Pair p = {3, 1.5f};
size_t n = sizeof(@inst(int, float) Pair);
int m = @inst(long) max(5, 3);
```

### Spellings

Three spellings name the same instance. Use whichever reads best; they mix freely in one file.

| Spelling | Use |
|---|---|
| `@inst(int, float) Pair` | Prefix: mirrors the `@template(A, B)` definition |
| `Pair@(int, float)` | Postfix: the name comes first, so nested arguments read left to right |
| `@inst Pair` | All defaults: leaves out the parentheses; `Pair@()` is the postfix form |

```c
Pair@(int, Vec@(char)) p;          // struct Pair__int__struct_Vec__char
int m = max@(long)(5, 3);          // call: the arguments, then the call parentheses
@inst Arr a;                       // every parameter defaulted
```

Whitespace and line breaks between the tokens are ignored, so `Vec @(int)` and `Vec @ ( int )` are the same instance. `@inst(int)` always takes the name that follows its parentheses: `Vec @inst(int) v` reads `v` as the template name, and the error says to write `Vec@(int)` or `@inst(int) Vec`.

Writing both argument lists (`@inst(int) Vec@(int)`) or `@inst(int)` with no name after it is an error.

The first use of an argument list instantiates the template; later uses share that instance. A struct, union, enum or typedef template names a type, a function template names a function; using one as the other is an error.

### Marker spellings

Each marker also has an attribute and a keyword spelling, and all mix freely in one file:

| `@` | Attribute | Keyword |
|---|---|---|
| `@template(T)` | `[[cccc::template(T)]]` | `__template(T)` |
| `@specialize(int)` | `[[cccc::specialize(int)]]` | `__specialize(int)` |
| `@inst(int) Vec` | | `__inst(int) Vec` |

```c
__template(T) struct Box { T v; };
[[cccc::specialize(bool)]] struct Box { unsigned bits; };

__inst(int) Box b = {1};
```

The attribute form takes `cccc::` or no namespace (`[[template(T)]]`, `[[cccc::template(T)]]`, `__attribute__((template(T)))`) and must be the only attribute in its list. The postfix `Name@(int)` has no other spelling.

### Pre-defining and aliasing

At file scope, `@inst(Type, ...) Name;` creates the instance without using it. This is how a derive attribute's functions come into existence when nothing else names the instance. A `typedef` or `#define` shortens repeated uses:

```c
@inst(int) Cell;                   // Cell__int and Cell__int_equals exist
typedef @inst(int) Vec IntVec;     // type alias
#define max_int @inst(int) max     // function alias
```

## Specialization

`@specialize(Args) <definition>` replaces the template's body for one argument list. It takes the arguments `@inst` does, defaults included, and the same kind of definition as the template: a struct, union, enum, typedef or function.

```c
@template(T) struct Vec { T *data; size_t len; };
@specialize(bool) struct Vec { uint64_t bits; size_t len; };

@template(T) T pick(T a, T b) { return a > b ? a : b; }
@specialize(bool) bool pick(bool a, bool b) { return a || b; }

@inst(bool) Vec v = {5, 2};      // the specialization: struct Vec__bool
@inst(int) Vec w = {0};          // the template: struct Vec__int
```

| Rule | Detail |
|---|---|
| Order | The template comes first; a specialization comes before the first `@inst` of its argument list |
| Kind | Matches the template: a struct template takes a struct specialization |
| Body | Plain C. The template's parameter names are not visible, and the template's own name stands for the instance as it does in a template body |
| Attributes | Only those written on the specialization: `@specialize(int) @equals struct Cell {...}` generates `Cell__int_equals` |
| Signature | A function specialization is not checked against the template's signature |
| Unused | A specialization nobody instantiates is never compiled |
| Where | File scope only, in runtime code or [comptime code](macros-templates.md#templates-in-comptime-code) |
| Errors | `redefinition of specialization 'Vec__bool'`, `specialization of 'Vec__bool' after its first use`, `'Vec' is a struct template, not a union` |

A specialization in a header applies in every file that includes it.

### Partial specialization

`@template(V...) @specialize(Pattern...)` covers every argument list that fits a pattern. The `@template` parameters are the pattern's variables; the body sees them, bound to the matched parts, and not the primary template's parameter names.

```c
@template(T) struct Vec { T *data; size_t len; };
@template(T) @specialize(T *) struct Vec { T **slots; size_t n; };         // any pointer
@template(T) @specialize(const T *) struct Vec { const T **ro; };          // a narrower pattern

@template(T, int N) struct Arr { T v[N]; };
@template(T) @specialize(T, 4) struct Arr { T a, b, c, d; };               // one length
@template(int N) @specialize(char, N) struct Arr { char text[N + 1]; };    // N bound by the argument
@template(T) @specialize(T, T) struct Two { T both[2]; };                  // repeated variable

@inst(char *) Vec p;     // the T * partial: struct Vec__char_ptr
@inst(const char *) Vec q;  // the const T * partial
```

A pattern may name an instance of a struct, union or enum template, and an array length may be a variable:

```c
@template(A, B) struct Pair { A a; B b; };
@template(T, U) @specialize(Pair@(T, U)) struct Vec { T first; U second; };  // any Pair
@template(T) @specialize(Pair@(T, T)) struct Vec { T both[2]; };            // a Pair of one type

@template(T, int N) @specialize(T[N]) struct Box { T items[N]; };           // any array length
@template(T, int N) @specialize(T[N], N) struct Arr { T v[N]; };            // length and N agree

Vec@(Pair@(int, float)) a;   // the first partial
Vec@(Pair@(int, int)) b;     // the second: it is the more specialised
Box@(int[3]) c;              // N is 3
```

| Rule | Detail |
|---|---|
| Pattern | A type pattern may use pointers, qualifiers, arrays (`T[4]`, `T[N]`), function types, blocks and template instances (`Pair@(T, U)`) around a variable. A constant position is a value (`4`) or a bare variable (`N`); an array length is a value or a bare variable, not an expression (`T[N + 1]`) |
| Selection | An exact `@specialize` wins; otherwise the most specialised matching partial; otherwise the template. Matching partials where none is narrower than all the others are an error |
| Constants | A constant variable has an integer, `float`, `double` or pointer type and binds a parameter of that kind (`@template(double D) @specialize(D, D)`, `@template(int *P) @specialize(P, P)`). It matches a value it holds exactly: `float F` takes `0.5`, not `0.1`; an object pointer variable takes a pointer to a compatible type, or null. An array length is an integer. Another kind is an error: `'N' has integer type and cannot bind 'D', which has floating type` |
| Variables | Every variable appears in the pattern; no defaults. A variable that appears twice stands for one type or one value (`T, T`, `N, N`, `T[N], N`), and a pack variable for one argument list (`Pack@(Ts...), Ts...`). A pattern leaves an argument out only when its default names no earlier parameter |
| Packs | A pattern over a [variadic template](templates-variadic.md#partial-specialization) ends in a pack variable (`@specialize(T *, Ts...)`, `@specialize(Ts *...)`) |
| Order | After the template, before the first `@inst` the pattern covers |
| Name | The instance keeps its argument-list name (`Vec__char_ptr`), and its linkage follows the template |
| Errors | `ambiguous partial specializations of 'Two' for 'Two__int_ptr__int_ptr'`, `redefinition of partial specialization of 'Vec'`, `a partial specialization must be more specific than the primary template`, `partial specialization parameter 'U' is not deduced by the pattern` |

## Instance names

An instance is named `Name__Arg` for each argument, built from the argument's resolved type, so `size_t` and `unsigned long` are the same instance. Use the mangled name directly once the instance exists.

| Argument | Spelling in the name |
|---|---|
| `int`, `unsigned int`, `long long`, `unsigned char` | `int`, `unsigned_int`, `long_long`, `unsigned_char` |
| `size_t` (an alias of `unsigned long`) | `unsigned_long` |
| Constant `16`, `-3` | `16`, `n3` |
| Enum constant `GREEN`, bool `1` | `GREEN`, `true` |
| Float `1.5`, `-0.25`, `1e-5`, `-0.0`, infinity | `1p5`, `n0p25`, `1em05`, `n0`, `inf` |
| Function `cmp_int`, object `table`, null | `cmp_int`, `table`, `null` |
| `int *`, `const char *`, `int * const` | `int_ptr`, `const_char_ptr`, `int_ptr_const` |
| `int[4]` | `int_arr4` |
| `struct Point`, `union U`, `enum E` | `struct_Point`, `union_U`, `enum_E` |
| `typedef struct {...} P;` (no tag) | `typedef_P` |
| `struct S` declared inside function `f` | `struct_S__in_f`; a second `struct S` in `f` is `struct_S__in_f__2` |
| `int *[[cccc::single]]`, `[[cccc::array]]`, `[[cccc::ntarray]]` | `int_ptr_single`, `int_ptr_array`, `int_ptr_ntarray` |
| `int _Checked[4]`, `char _Nt_checked[6]` | `int_arr4_checked`, `char_arr6_ntchecked` |
| `[[cccc::global]] int *` | `global_int_ptr` |
| `int (^)(int)` | `fn_int_of_int_end_block` |
| Enumerator of an enum template `Level`, with `char` | `Level__char__HIGH` |
| `void (*)(int)` | `fn_void_of_int_end_ptr` |

```c
@inst(int, float) Pair a;
struct Pair__int__float b;     // the same type

@inst(size_t) Vec c;
struct Vec__unsigned_long d;   // the same type
```

A checked pointer's bounds (`count(n)`) are not part of the argument, so pointers that differ only in bounds share an instance. A tagged `struct P` and an untagged `typedef struct {...} P` are different instances (`struct_P`, `typedef_P`), and so are two `struct S` declared in different functions.[^blockscope] Arguments that still mangle alike but name different types (`struct a_ptr` and `struct a *`) are an error.

## Inside a template

Each parameter names its argument type throughout the body, so `T a, b;` with `T = int *` declares two pointers. The template's own name refers to the instance, and a template may instantiate itself or others:

```c
@template(T) struct Node { T val; struct Node *next; };
@template(T) T fact(T n) { return n <= 1 ? 1 : n * @inst(T) fact(n - 1); }
@template(T) struct Box { @inst(T, int) Pair inner; };
```

Instantiation nests up to 64 levels deep.

## Preprocessed output

`cccc -E` prints the source spellings `@template(T)`, `@specialize(A)` and `Name@(Args)`, each on one line, so the output compiles again: `@inst(int) max(40, 42)` prints as `max@(int)(40, 42)`.

## Functions and linkage

A function instance is shared: the program has one copy, and a pointer to it compares equal in every translation unit. Two files that instantiate the same template, or a header that holds one, link cleanly.[^linkage]

| Written | Instance |
|---|---|
| `@template(T) T max(...)` | One shared copy |
| `@template(T) static T max(...)` | A private copy per translation unit |

Two translation units whose templates of the same name produce the same instance are an error (`conflicting definitions of template instance 'max__int'`), as is an ordinary function with the instance's mangled name.

A function instance whose arguments include a type declared inside a function is private to its translation unit, like a `static` template: two files that each declare their own local `struct S` get two instances.

## Derive attributes

Attributes written on a template run on each instance, after instantiation. The generated functions carry the mangled name:

```c
@template(T) @equals struct Cell { T a; T b; };

@inst(int) Cell x = {1, 2}, y = {1, 2};
_Bool same = Cell__int_equals(&x, &y);
```

## Limits

| Not supported | Result |
|---|---|
| `long double` or `_BitInt` over 64 bits constant parameter, a NaN argument | Compile error |
| Pattern naming an instance of a function or typedef template, or a variable inside an array-length expression (`T[N + 1]`) | Compile error |
| Argument that is a variable-length array | Compile error |
| Argument that is an unnamed struct, union or enum with no typedef name | Compile error asking for a tag or typedef name |
| A runtime template used in comptime code, or the reverse | Each program has its own templates: `'twice' is not a template in comptime code` |
| Defining a template after its first `@inst` | The template must come first |

## Diagnostics

An error inside an instance points at the line in the template body and adds a note naming the instance and the `@inst` site:

```
error: not a struct nor a union
  note: in instantiation of template 'bad' (bad__int) at main.c:7:13
```

Using an instance whose body had an error again reports the first error: `instantiation of 'bad__int' failed earlier: not a struct nor a union`.

[^linkage]: A shared instance is a weak symbol named by its mangled name in objects written with `-c=object`, so separately compiled files merge when linked. A whole program built with `-c=native` keeps it `static`, which lets the host compiler inline it.

[^blockscope]: A type declared inside a function is named after that function (`struct_S__in_f`), and numbered in declaration order from the second same-spelled type in it (`__2`). A type inside a block literal or an OpenMP region takes the enclosing function's name, and one in a GNU nested function `g` of `f` takes `f_g`. A type declared in a parameter list cannot be a template argument. Types declared at file scope are never suffixed and have the same name in every file.

[^enumname]: Enumerators sharing a value name one instance, by the first enumerator declared with it. When a value without an enumerator would be spelled like an existing enumerator (`n3` for `-3`), the instantiation is an error.

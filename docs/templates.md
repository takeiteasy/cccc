# Templates

Generic structs, unions and functions: write the definition once with type parameters, instantiate it per type.

```c
@template(T) struct Vec { T *data; size_t len, cap; };
@template(T) T max(T a, T b) { return a > b ? a : b; }

int main(void) {
    @inst(Vec, int) v = {0};          // struct Vec__int
    return @inst(max, int)(40, 42);   // max__int
}
```

## Defining a template

`@template(P, ...)` goes before a file-scope struct, union or function **definition**. The body is kept as tokens and is not compiled until it is instantiated, so a template that is never used costs nothing and never reaches the output.

| Rule | Detail |
|---|---|
| Parameters | One or more type names: `@template(K, V)` |
| Allowed on | A struct, union or function definition at file scope |
| Not allowed on | Prototypes, `typedef`, `extern`, enums, a struct that declares variables, anything inside a function |
| Attributes | Other attributes on the declaration stay with it and apply to every instance: `@template(T) @equals struct Cell { T a; T b; };` |
| Name | One template per name; a second definition is an error |

## Instantiating

`@inst(Name, Type, ...)` names an instance. It works wherever a type or an expression is allowed, at file scope or inside a function: declarations, `sizeof`, casts, compound literals, parameter lists and other templates.

```c
@inst(Pair, int, float) p = {3, 1.5f};
size_t n = sizeof(@inst(Pair, int, float));
int m = @inst(max, long)(5, 3);
```

The first use of an argument list instantiates the template; later uses share that instance. A struct or union template names a type, a function template names a function; using one as the other is an error.

### Pre-defining and aliasing

At file scope, `@inst(Name, Type, ...);` creates the instance without using it. This is how a derive attribute's functions come into existence when nothing else names the instance. A `typedef` or `#define` shortens repeated uses:

```c
@inst(Cell, int);                   // Cell__int and Cell__int_equals exist
typedef @inst(Vec, int) IntVec;     // type alias
#define max_int @inst(max, int)     // function alias
```

## Instance names

An instance is named `Name__Arg` for each argument, built from the argument's resolved type, so `size_t` and `unsigned long` are the same instance. Use the mangled name directly once the instance exists.

| Argument | Spelling in the name |
|---|---|
| `int`, `unsigned int`, `long long`, `unsigned char` | `int`, `unsigned_int`, `long_long`, `unsigned_char` |
| `size_t` (an alias of `unsigned long`) | `unsigned_long` |
| `int *`, `const char *`, `int * const` | `int_ptr`, `const_char_ptr`, `int_ptr_const` |
| `int[4]` | `int_arr4` |
| `struct Point`, `union U`, `enum E` | `struct_Point`, `union_U`, `enum_E` |
| `void (*)(int)` | `fn_void_of_int_end_ptr` |

```c
@inst(Pair, int, float) a;
struct Pair__int__float b;     // the same type

@inst(Vec, size_t) c;
struct Vec__unsigned_long d;   // the same type
```

## Inside a template

Each parameter names its argument type throughout the body, so `T a, b;` with `T = int *` declares two pointers. The template's own name refers to the instance, and a template may instantiate itself or others:

```c
@template(T) struct Node { T val; struct Node *next; };
@template(T) T fact(T n) { return n <= 1 ? 1 : n * @inst(fact, T)(n - 1); }
@template(T) struct Box { @inst(Pair, T, int) inner; };
```

Instantiation nests up to 64 levels deep.

## Functions and linkage

A function instance is `static`, so each translation unit has its own copy and two files that instantiate the same template link cleanly. A function pointer to the same instance therefore compares unequal across translation units.[^linkage]

## Derive attributes

Attributes written on a template run on each instance, after instantiation. The generated functions carry the mangled name:

```c
@template(T) @equals struct Cell { T a; T b; };

@inst(Cell, int) x = {1, 2}, y = {1, 2};
_Bool same = Cell__int_equals(&x, &y);
```

## Limits

| Not supported | Result |
|---|---|
| Constant parameters (`@template(T, int N)`) | Compile error |
| Specialization | A template has one definition |
| Enum templates, `typedef` templates | Compile error |
| Argument that is a checked pointer, address-space type, block, or variable-length array | Compile error naming the kind |
| Argument that is an unnamed struct, union or enum | Compile error asking for a tag |
| `@template` or `@inst` in comptime code | Compile error |
| Defining a template after its first `@inst` | The template must come first |

## Diagnostics

An error inside an instance points at the line in the template body and adds a note naming the instance and the `@inst` site:

```
error: not a struct nor a union
  note: in instantiation of template 'bad' (bad__int) at main.c:7:13
```

[^linkage]: A template in a header is safe to include from several files for the same reason. Sharing one definition across translation units is a possible refinement.

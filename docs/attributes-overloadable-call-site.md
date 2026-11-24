# Call-Site Attributes

Clang's `enable_if`, `diagnose_if` and `pass_object_size` look at each call's arguments. `enable_if` decides whether a function can be called, `diagnose_if` reports an error or warning about a call, and `pass_object_size` hands the callee the size of the object a pointer argument points into.

| Attribute | Spelling | Applies to |
|---|---|---|
| `enable_if(cond, "msg")` | `__attribute__` only | A function |
| `diagnose_if(cond, "msg", "error"\|"warning")` | `__attribute__` only | A function |
| `pass_object_size(type)` | `__attribute__`, `[[clang::]]` | A `const` pointer parameter |
| `pass_dynamic_object_size(type)` | `__attribute__`, `[[clang::]]` | A `const` pointer parameter |

`__has_attribute` is 1 for all four. `__has_c_attribute(clang::pass_object_size)` is 1, and `__has_c_attribute(clang::enable_if)` is 0. Like any Clang attribute, `enable_if` and the others also work on functions without `overloadable`.

## Conditions

A condition is an expression over the function's parameters. At each call it is evaluated with the call's arguments, converted to the parameter types. A condition holds only when the result is a constant and nonzero, so a condition that reads an argument which is not a constant does not hold.

```c
__attribute__((enable_if(n < 10, "n must be below 10")))
int digit(int n);

digit(4);   // fine
digit(20);  // error: no matching function for call to 'digit'
            //     candidate disabled: n must be below 10
int k = read();
digit(k);   // error too: k is not a constant
```

Two builtins inside a condition apply to the argument:

| In a condition | Means |
|---|---|
| `__builtin_constant_p(p)` | Whether the argument for `p` is a constant |
| `__builtin_object_size(p, type)` | The object size of the argument for `p` ([pass_object_size](#pass_object_size)) |

```c
__attribute__((overloadable))
__attribute__((enable_if(n <= __builtin_object_size(dst, 0), "fits")))
int fill(char *const dst, int n);   // chosen only when n fits in the buffer
```

## `enable_if`

A call can select a function only if every `enable_if` it has holds. In an overload set, the functions whose conditions fail drop out before ranking. A call with no function left is `no matching function`, and each candidate that was dropped says why:

```text
error: no matching function for call to 'sign'
    candidate: sign(int n) (disabled: positive only)
    candidate: sign(int n) (disabled: very negative)
```

When the arguments rank two functions alike, the one whose `enable_if` list starts with the other's and is longer wins. Lists that differ in any other way make the call ambiguous.

```c
__attribute__((overloadable))
__attribute__((enable_if(n > 0, "positive")))
int sign(int n) { return 1; }

int __attribute__((overloadable)) sign(int n) { return 0; }

sign(3);    // 1: both match, and the first has the longer list
sign(-3);   // 0
```

Taking the address of a function with an `enable_if` is an error unless every condition is a constant that holds.

## `diagnose_if`

`diagnose_if` reports its message at a call whose arguments make the condition hold. With `"error"` the call is an error. With `"warning"` it is a warning under `-Wuser-defined-warnings`, which is an alias of `-Wattributes` and part of `-Wall`.

```c
__attribute__((diagnose_if(d == 0, "division by zero", "warning")))
int divide(int n, int d);

divide(10, 0);  // warning: division by zero [-Wattributes]
divide(10, x);  // no warning: x is not a constant
```

It has no effect on which function is chosen. Naming the function without calling it, as in `int (*f)(int, int) = divide;`, reports the conditions that hold without arguments. A call in a statically dead branch is not reported, as for `error("msg")`.

## `pass_object_size`

Each call passes the size of the object the argument points into, as a hidden `size_t` after that argument. Inside the function, `__builtin_object_size(p, type)` on the parameter returns that size, so a wrapper sees the caller's buffer:

```c
#define PS(type) __attribute__((pass_object_size(type)))

size_t room(const char *const p PS(0)) {
    return __builtin_object_size(p, 0);
}

char buf[23];
room(buf);      // 23
room(buf + 3);  // 20
room(getenv("HOME"));  // (size_t)-1: not known
```

| Form | Caller passes |
|---|---|
| `pass_object_size(type)` | `__builtin_object_size(arg, type)` |
| `pass_dynamic_object_size(type)` | `__builtin_dynamic_object_size(arg, type)`, which also knows heap allocations[^dynamic] |

The attribute may follow the parameter name, come before the declaration, or follow the pointer's qualifiers (`void *const __attribute__((pass_object_size(0))) p`, as the macOS SDK writes it). Every declaration of the function must agree on it.

| Error | Example |
|---|---|
| The parameter is not a `const` pointer | `const char *p PS(0)` |
| The type is not 0 to 3 | `pass_object_size(4)` |
| Two on one parameter | `PS(0) PS(1)` |
| Declarations disagree | A prototype with the attribute, a definition without |
| Taking the address | `size_t (*f)(const char *) = room;` |

In an overload set, a function with a `pass_object_size` parameter wins over an otherwise equal one without.

## Names in the output

The hidden size is a real parameter, as in Clang, so `-m` and `-c=native` print it, and a function compiled by Clang links against a caller compiled by CCCC and the other way round. An `overloadable` function's name carries the attributes as Clang mangles them:

| Declaration | Name |
|---|---|
| `int f(int a) __attribute__((enable_if(a > 0, "")))` | `_Z1fUa9enable_ifIXgtfL0p_Li0EEEi` |
| `int k(const void *const p PS(0))` | `_Z1kPKvU17pass_object_size0` |

A function without `overloadable` keeps its plain name.

[^dynamic]: A heap size comes from the VM's allocation header. Under `-c=native` the host compiler's `__builtin_dynamic_object_size` answers, which knows a `malloc` size only when the optimizer sees the allocation ([native-differences.md](native-differences.md)). A dynamic size reads the argument a second time, so an argument with side effects passes the static size instead.

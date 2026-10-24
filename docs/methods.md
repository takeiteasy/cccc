# Struct methods

`[[cccc::method(T)]]` on a free function makes it callable as `s.f(x)`. The parser rewrites the call to `f(&s, x)`, so the VM and `-c=native` behave the same.

```c
typedef struct { float x, y; } Vec2;

[[cccc::method(Vec2)]]
float len(Vec2 *self) { return sqrtf(self->x * self->x + self->y * self->y); }

Vec2 v = {3, 4};
Vec2 *pv = &v;
float a = v.len();    // len(&v)
float b = pv->len();  // len(pv)
```

## Declaring

| Form | Method name |
|---|---|
| `[[cccc::method(Vec2)]] float len(Vec2 *self)` | `len`, the function's name |
| `[[cccc::method(Vec2, len)]] float vec2_len(Vec2 *self)` | `len` |

The optional second argument names the method, so two structs can each have a `len` while their C functions keep distinct names:

```c
[[cccc::method(Vec2, len)]] float vec2_len(Vec2 *self);
[[cccc::method(Vec3, len)]] float vec3_len(Vec3 *self);
```

The attribute works on structs and unions you do not own. The function is declared before the call that uses it.

## Defining in the struct

A function written in a struct or union body is a method of that type. It becomes an ordinary file-scope function named `<T>_<f>` and adds no member, so the layout is unchanged.

```c
typedef struct Vec2 {
    float x, y;
    float len(Vec2 *self) { return sqrtf(self->x * self->x + self->y * self->y); }
    float dot(Vec2 *self, Vec2 *o);   // defined later as Vec2_dot
} Vec2;

Vec2 v = {3, 4};
v.len();         // Vec2_len(&v)
Vec2_len(&v);    // the same function, callable and addressable by name
```

| Rule | Behavior |
|---|---|
| Function name | the typedef name, else the tag: `typedef struct v {...} V;` gives `V_len`; `struct S {...};` gives `S_len` |
| Receiver | the explicit first parameter, as in the attribute form |
| Bodies | parsed after the declaration, so they can use `sizeof(T)`, members and methods defined later |
| `static` | passed through |
| `inline` | becomes `static inline`, so a struct in a shared header links |
| Block scope | not allowed: `methods are only allowed in file-scope structs and unions` |
| No name | `a struct with methods needs a tag or a typedef name` |

A function-pointer member (`int (*cb)(int);`) is still a member.

## Receiver

The first parameter is the receiver, written out in the signature; there is no implicit `self`.

| First parameter | `s.f()` passes | `p->f()` passes |
|---|---|---|
| `T *` / `const T *` | `&s` | `p` |
| `T` | `s` | `*p` |

An rvalue receiver (`make().len()`) passed to a `T *` parameter is copied into a temporary first, so writes through the pointer are discarded.

## Name lookup

A member named `f` wins: `s.f(x)` is a method call only when `T` has no member `f`. Methods are keyed by (`T`, name), and the receiver type must match exactly.

## Errors

| Case | Diagnostic |
|---|---|
| `T` is not a struct or union | `method receiver type must be a struct or union` |
| First parameter is not `T`, `T *` or `const T *` | `method 'f' must take the receiver type as its first parameter` |
| Two functions for the same (`T`, name) | `method 'f' is already defined for this type` |
| No member or method `f` | `no such member 'f'` |
| Method used without a call (`auto g = s.f;`) | `'f' is a method; call it as a method or name the function directly` |
| In-struct function with no parameters | `method 'f' must take the receiver type as its first parameter` |

Operators for the same structs are in [operators.md](operators.md).

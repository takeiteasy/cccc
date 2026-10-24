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

Operators for the same structs are in [operators.md](operators.md).

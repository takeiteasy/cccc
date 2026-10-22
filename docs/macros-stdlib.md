# Macro Standard Library

Ready-made macros bundled in `reflection.h`.

## Macro Standard Library

`include/cccc/reflection.h` ships a bundled set of helpers for common
macro-author tasks. Because reflection.h is implicitly included in every
macro/comptime TU, these are available without any extra `#include`. The
ready-made attributes built on them (`@serialize`, `@enum_to_string`,
`@accessors`, ...) are covered in [derive.md](derive.md).

### String and Memory Wrappers

Thin AST wrappers over `memcpy`, `strlen`, and `strcmp`:

```c
Node *dst, *src, *n;
Memcpy(dst, src, n);      // memcpy(dst, src, n)
Node *str;
Strlen(str);              // strlen(str)
Node *a, *b;
Strcmp(a, b);             // strcmp(a, b)
```

`<string.h>` is always declared in the comptime TU so these work without
additional includes.

### `ForeachMember(type, varname, body)`

Host-side loop over every struct/union member:

```c
[[cccc::comptime]]
void print_offsets(void) {
    Type *ty = GetType("MyStruct");
    ForeachMember(ty, m, {
        // m is a Member* — use MemberName(m), MemberOffset(m), etc.
        int off = MemberOffset(m);
    });
}
```

Each iteration executes `body` at compile time with `varname` bound to the
current `Member *`.

### `OffsetofChain(type, ...)`

Sum offsets through a chain of nested struct fields:

```c
Type *ty = GetType("Outer");
int off = OffsetofChain(ty, "inner", "x");
// equivalent to offsetof(Outer, inner) + offsetof(Inner, x)
```

Returns `-1` if any field name in the chain is not found.

### Serialization Helpers

#### `Serialize(type, expr, buf)` / `Deserialize(type, buf)`

Build a block that copies a struct into a raw byte buffer, and an expression
that rebuilds it from one. Both copy bytes member by member, so the buffer
needs no particular alignment:

```c
[[cccc::comptime]]
Node *ser(Node *val, Node *buf) {
    return Serialize(GetType("Point"), val, buf);
}
[[cccc::comptime]]
Node *deser(Node *buf) {
    return Deserialize(GetType("Point"), buf);
}
```

Scope: scalars and flat structs (nested flat structs, bit-fields and the
fields of anonymous struct/union members are copied; pointer-typed members are
copied as raw pointer bytes). The buffer holds each member at its natural
offset; padding is not written, and `Deserialize` zeroes the padding of the
value it returns.

### Enum String Conversion

#### `EnumToString(type, expr)` / `EnumFromString(type, expr)`

Build a switch or if-chain that converts between enum values and their name
strings:

```c
[[cccc::comptime]]
Node *color_name(Node *v) {
    return EnumToString(GetType("Color"), v);
}
```

### FP-Style Array Generators

`GenerateSum`, `GenerateMap`, `GenerateReduce`, and `GenerateFilter`
publish typed array helpers for a given element type. Call from a comptime
function invoked at file scope:

```c
[[cccc::comptime]]
void setup_int_helpers(void) {
    Type *int_ty = GetType("int");
    GenerateSum(int_ty);    // int sum_int(int *arr, size_t n)
    GenerateMap(int_ty);    // void map_int(int *arr, size_t n, int *out, int (*f)(int))
    GenerateReduce(int_ty); // int reduce_int(int *arr, size_t n, int init, int (*f)(int, int))
    GenerateFilter(int_ty); // void filter_int(int *arr, size_t n, int *out, size_t *out_n, bool (*pred)(int))
}
setup_int_helpers();
```

`GenerateMap` and `GenerateFilter` use caller-provided output buffers
(no `malloc`). `GenerateFilter` sets `*out_n` to the number of elements
written.

| Generator | Signature |
|---|---|
| `GenerateSum(T)` | `T sum_T(T *arr, size_t n)` |
| `GenerateMap(T)` | `void map_T(T *arr, size_t n, T *out, T (*f)(T))` |
| `GenerateReduce(T)` | `T reduce_T(T *arr, size_t n, T init, T (*f)(T, T))` |
| `GenerateFilter(T)` | `void filter_T(T *arr, size_t n, T *out, size_t *out_n, bool (*pred)(T))` |

Each generator publishes once per element type; repeating a call, from the same
or another comptime function or input file, does nothing.

The function name suffix is derived from the element type's canonical C name
(`int`, `ulong`, `double`, etc.) or its user-defined type name for struct/enum
types.

### Reflection helpers

| Task | API |
|------|-----|
| Iterate all struct/union members | `ForeachMember(ty, var, body)` |
| Nested-field byte offset | `OffsetofChain(ty, "field", ...)` |
| Flat struct serialization | `Serialize(ty, expr, buf)`, `Deserialize(ty, buf)` |
| Enum → string switch | `EnumToString(ty, expr)` |
| String → enum if-chain | `EnumFromString(ty, expr)` |
| Generate typed sum/map/reduce/filter | `GenerateSum(T)`, `GenerateMap(T)`, `GenerateReduce(T)`, `GenerateFilter(T)` |

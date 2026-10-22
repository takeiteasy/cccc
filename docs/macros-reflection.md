# Type and Symbol Reflection

Inspect types, members and symbols at compile time.

## Type And Symbol Reflection

The handle types (`Node`, `Type`, `Obj`, `Token`, `Member`, `EnumConstant`,
`AttrTarget`, `Builder`, `BuildTarget`) are opaque typedefs. Their struct tags
are reserved, so your own `struct Node`, `union Type` or `enum Obj` coexists
with them; only a *typedef* named `Node` etc. in your own code is shadowed
inside comptime code.

Macros can inspect types and global symbols that are visible at the macro
execution point.

### `$identifier` — Compile-Time Reflect Operator

The `$identifier` expression resolves a name to a compile-time pointer at
parse time:

| Expression | Result type | Resolves to |
|------------|-------------|-------------|
| `$TypeName` | `Type *` | The `Type *` for a typedef alias or struct/union/enum tag |
| `$varname` | `Obj *` | The `Obj *` for a global variable |
| `$fnname` | `Obj *` | The `Obj *` for a function |

The name must be visible in the current scope; an unknown name is a
compile-time error.

`$` followed by `{` is the backtick-splice operator (not reflect). Splice
placeholders inside `Quote` templates (`$1`, `$$`, `$@`) are also distinct.

```c
typedef struct { int x; int y; } Point;
int counter = 0;

[[cccc::comptime]]
Node *reflect_type_kind(void) {
    Type *ty = $Point;                       // Type* for Point
    return MakeIntLiteral(GetTypeKind(ty));  // TK_STRUCT
}

[[cccc::comptime]]
Node *reflect_var(void) {
    Obj *obj = $counter;                     // Obj* for counter
    return MakeIntLiteral(obj ? 1 : 0);
}
```

`$SomeType` can also be used directly as a call argument to a comptime inline
macro, avoiding an intermediate variable:

```c
[[cccc::comptime]]
Node *describe(Type *ty) {
    return MakeIntLiteral(GetTypeKind(ty));
}

// At the call site:
int kind = describe($Point);
```

```c
typedef enum { RED, GREEN, BLUE } Color;

[[cccc::comptime]]
Node *color_count(void) {
    Type *color = FindType("Color");
    if (!color)
        return MakeIntLiteral(-1);
    return MakeIntLiteral(EnumCount(color));
}

int main(void) {
    return color_count();
}
```

Useful reflection entry points include:

| Task | API |
|------|-----|
| Find a type by name (NULL if absent) | `FindType(name)` |
| Get a base, typedef, or tag type (errors if absent) | `GetType(name)` |
| Count enum constants | `EnumCount(ty)` |
| Read enum constants | `EnumAt(ty, i)`, `EnumConstantName(ec)`, `EnumConstantValue(ec)` |
| Count struct/union members | `StructMemberCount(ty)` |
| Read members | `StructMemberAt(ty, i)`, `MemberName(m)`, `MemberType(m)`, `MemberOffset(m)` |
| Find globals | `FindGlobal(name)`, `GlobalCount()`, `GlobalAt(i)` |

`GetType(name)` resolves any standard base-type spelling regardless of word
order — `"int"`, `"unsigned char"`, `"long long"`, `"signed int"`, `"long
double _Complex"`, `"bool"` — before falling back to a typedef or tag lookup.
It is the right call when the result feeds straight into
`MakeArray`/`MakeConst`/`GlobalVar`: an unresolved name is a hard comptime
error rather than a `NULL` that silently corrupts the generated declaration.
Use `FindType(name)` or `TypeExists(name)` to probe for a type that may not
exist.

For call-site macro expansion, `MakeVarRef(name)` and `FindType(name)`
use the lexical scope where the macro call appears, including nested block
locals and typedefs. When a macro receives an expression argument and needs the
exact variable passed by the caller, prefer inspecting the argument node itself
instead of looking it up again by string name.

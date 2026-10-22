# Custom Attribute Handlers

Comptime functions that run when a declaration carries a user-defined attribute.

## Custom Attributes

Comptime macros can register declaration attributes for file-scope declarations:

```c
@comptime(attribute("serialize"))
void define_serializer(AttrTarget *target) {
    Type *ty = $ATTR_TARGET_TYPE(target);
    const char *name = AttrTargetName(target);

    Obj *fn = MakeFunction("serialize_Point", GetType("int"));
    FunctionAddParam(fn, "p", MakePointer(ty));
    WithFn(fn) {
        FunctionSetBody(fn, Quote("return sizeof(struct Point);"));
    }
    PublishNode(fn);
}

@serialize
struct Point { int x; int y; };
```

The handler's first parameter is always `AttrTarget *`. Fixed parameters
after that receive attribute arguments as `Node *` expression nodes, so
`@answer(123)` calls a handler shaped like:

```c
@comptime(attribute("answer"))
void answer_attr(AttrTarget *target, Node *value) { ... }
```

A handler reads a string argument with `StringLiteralValue(node)` and an
integer argument with `ConstIntValue(node, &out)`, which accepts any integer
constant expression (`3`, `-1`, `N * 2`, an enum constant) and returns false for
anything else:

```c
@comptime(attribute("weight"))
void weight_attr(AttrTarget *target, Node *value) {
    int64_t w;
    if (!ConstIntValue(value, &w))
        MacroErrorAt(value, "@weight expects an integer constant");
}
```

Variadic attribute handlers use the same `$ast_vararg_*` helpers as inline
macros. The target helpers are:

| Helper | Description |
|---|---|
| `GetAttrTargetKind(target)` | `ATTR_TARGET_TYPE`, `ATTR_TARGET_FUNCTION`, `ATTR_TARGET_GLOBAL`, `ATTR_TARGET_TYPEDEF`, or `ATTR_TARGET_MEMBER` |
| `AttrTargetName(target)` | Source name when available |
| `$ATTR_TARGET_TYPE(target)` | Target type (the member's type for a member) |
| `AttrTargetObj(target)` | Function/global object, or `NULL` for type/typedef/member targets |
| `AttrTargetToken(target)` | Source token for diagnostics |
| `AttrTargetMember(target)` | The `Member *` of a member target, else `NULL` |
| `AttrTargetOwner(target)` | The struct/union declaring a member target, else `NULL` |

Applying a custom attribute to a local variable is a compile error.

## Member attributes

A custom attribute can sit on a struct or union member, in any declarator
position:

```c
struct Account {
    int id @column("account_id");   // after the declarator
    @column("tag") int a, b;        // before the type: a and b both carry it
    int flags : 3 @column("flags"); // after a bit-field width
};
```

A handler registered for the name runs once per attributed member, after the
enclosing declaration is complete, so offsets are final and the owner's member
list is full. `a` and `b` each get their own run of the handler. An anonymous
struct/union member takes attributes from the declaration specifiers only.

A struct-level handler reads the attributes while it walks the members:

```c
@comptime(attribute("columns")) void columns(AttrTarget *target) {
    Type *ty = AttrTargetType(target);
    for (int i = 0; i < StructMemberCount(ty); i++) {
        Member *m = StructMemberAt(ty, i);
        if (MemberHasAttr(m, "column"))
            use(StringLiteralValue(MemberAttrArg(m, "column", 0)));
    }
}
```

| Helper | Description |
|---|---|
| `MemberHasAttr(m, "name")` | 1 if the member carries the attribute |
| `MemberAttrArgCount(m, "name")` | Argument count of the first use, `-1` if absent |
| `MemberAttrArg(m, "name", i)` | Argument `i` of the first use as a `Node *`, or `NULL` |
| `MemberAttrCount(m)` | Number of attribute uses on the member |
| `MemberAttrNameAt(m, i)` | Name of use `i`, or `NULL` |
| `MemberAttrArgCountAt(m, i)` | Argument count of use `i`, `-1` if out of range |
| `MemberAttrArgAt(m, i, j)` | Argument `j` of use `i`, or `NULL` |

The name-based helpers see the first use when a name repeats; the `At` helpers
walk every use. A member attribute needs a registered handler: a name with
none is dropped silently, as on any declaration, so a misspelled name does not
error. A member handler that has nothing to do can be an empty function.

Member attributes are not allowed on the members of a struct defined inside a
function body.

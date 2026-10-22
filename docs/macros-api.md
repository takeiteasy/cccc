# Macro API Reference

Lookup tables for the comptime API.

## API Reference

### Type APIs

| Convenience macro | Description |
|-------------------|-------------|
| `FindType(name)` | Find a typedef, struct, union, or enum by name |
| `TypeExists(name)` | Test whether a type name exists |
| `GetType(name)` | Get a base type (any spelling, e.g. `"unsigned char"`, `"long long"`), typedef, or tag; hard error if the name does not resolve |
| `GetTypeKind(ty)` | Get `TypeKind` |
| `TypeSize(ty)` | Get size in bytes |
| `TypeAlign(ty)` | Get alignment in bytes |
| `TypeIsUnsigned(ty)` | Test unsigned integer type |
| `TypeIsConst(ty)` | Test const-qualified type |
| `TypeBase(ty)` | Get pointer or array base type |
| `TypeArrayLen(ty)` | Get array length, or `-1` |
| `TypeReturnType(ty)` | Get function return type |
| `TypeParamCount(ty)` | Count function parameters |
| `TypeParamAt(ty, i)` | Get function parameter type |
| `TypeIsVariadic(ty)` | Test variadic function type |
| `TypeName(ty)` | Get a type name when available — for a tagged struct/union/enum, always the tag (never a typedef alias spelling, even when looked up by one) |
| `MakePointer(base)` | Create pointer type |
| `MakeArray(base, len)` | Create array type |

### Enum APIs

| Convenience macro | Description |
|-------------------|-------------|
| `EnumCount(ty)` | Count enum constants |
| `EnumAt(ty, i)` | Get enum constant at index |
| `EnumFind(ty, name)` | Find enum constant by name |
| `EnumConstantName(ec)` | Get enum constant name |
| `EnumConstantValue(ec)` | Get enum constant value |
| `EnumName(ty)` | Get enum type name |
| `EnumValueCount(ty)` | Count enum values |
| `EnumValueName(ty, i)` | Get enum value name |
| `EnumValue(ty, i)` | Get enum value |

### Struct And Union APIs

| Convenience macro | Description |
|-------------------|-------------|
| `StructMemberCount(ty)` | Count members |
| `StructMemberAt(ty, i)` | Get member at index |
| `StructMemberFind(ty, name)` | Find member by name |
| `MemberName(m)` | Get member name |
| `MemberType(m)` | Get member type |
| `MemberOffset(m)` | Get member offset |
| `MemberIsBitfield(m)` | Test bitfield member |
| `MemberBitfieldWidth(m)` | Get bitfield width |

### Global Symbol APIs

| Convenience macro | Description |
|-------------------|-------------|
| `FindGlobal(name)` | Find global object |
| `GlobalCount()` | Count globals |
| `GlobalAt(i)` | Get global at index |
| `ObjName(obj)` | Get object name |
| `ObjType(obj)` | Get object type |
| `ObjIsFunction(obj)` | Test function object |
| `ObjIsDefinition(obj)` | Test definition |
| `ObjIsStatic(obj)` | Test static linkage |

### AST Builder APIs

| Convenience macro | Description |
|-------------------|-------------|
| `MakeIntLiteral(val)` | Integer literal |
| `MakeFloatLiteral(val)` | Floating-point literal |
| `MakeStringLiteral(str)` | String literal |
| `StringLiteralValue(node)` | Text of a string literal argument, or `NULL` |
| `ConstIntValue(node, &out)` | Stores an integer constant expression argument in `out`; false if it is not one |
| `ConstUIntValue(node, &out)` | Like `ConstIntValue` into a `uint64_t`; accepts values up to `UINT64_MAX`, false for a negative signed value |
| `MakeVarRef(name)` | Variable reference |
| `MakeParamRef(fn, name)` | Generated function parameter reference |
| `Gensym(prefix)` | Unique arena-allocated symbol name using `__builtin_gensym` |
| `MacroExpand1(node)` | Single-step macro expansion (identity if not a macro call) |
| `MacroExpand(node)` | Full macro expansion — repeats until the top-level form is stable |
| `CurrentToken()` | Opaque token for the active macro call site |
| `SyntheticToken(label)` | Opaque synthetic token for generated diagnostics |
| `TokenFromNode(node)` | Opaque source token attached to a node |
| `SetToken(node, tok)` | Attach a token to a node and return the node |
| `CopyLocation(dst, src)` | Copy source location from one node to another |
| `CopyNode(node)` | Deep copy of an expression or statement tree (labels, `switch`, declarations, `cleanup` variables and VLAs included), for linking into a second list |
| `Quote(tmpl, ...)` | Parse a C template string, splicing `$N`/`$@N` argument nodes, immediately |
| `QuoteN(tmpl, nodes, count)` | Array-form `Quote`; validates the splice count |
| `QuoteLazy(tmpl, ...)` | Capture a template and its splice arguments without parsing; parsed only once spliced in, at the splice site's own scope/loop context |
| `QuoteLazyN(tmpl, nodes, count)` | Array-form `QuoteLazy` |
| `NodeList(arr, count)` | Build a `->next`-linked node chain from an array for use as a `$@k` splice argument |
| `MakeBinary(op, l, r)` | Binary expression |
| `MakeUnary(op, operand)` | Unary expression |
| `MakeCast(expr, ty)` | Cast expression |
| `MakeAssign(target, value)` | Assignment expression |
| `MakeMember(obj, name)` | Struct/union member expression |
| `MakeSubscript(arr, idx)` | Array subscript expression (`arr[idx]`) |
| `CompoundLiteral(ty, ...)` | Positional compound literal; stack-local in function scope, static global outside `WithFn` |
| `InitArray(elem_ty, ...)` | Array compound literal with explicit element type; length inferred from argument count |
| `InitStruct(ty, fields, values, n)` | Designated struct/union init; unspecified members are zero |
| `MakeFuncCall(callee, args, n)` | Function call expression |
| `VarargAsArray()` | Borrowed inline variadic argument array for forwarding |
| `MakeReturn(expr)` | Return statement |
| `MakeBlock(stmts, count)` | Compound statement |
| `BlockAddStmt(block, stmt)` / `BlockAddStmt(stmt)` | Append a statement to a block; shorthand form uses `WithBlock` |
| `MakeIf(cond, then_body, else_body)` | If statement |
| `MakeSwitch(cond)` | Switch statement |
| `SwitchAddCase(sw, value, body)` / `SwitchAddCase(value, body)` | Add switch case; shorthand form uses `WithSwitch` |
| `SwitchSetDefault(sw, body)` / `SwitchSetDefault(body)` | Set switch default; shorthand form uses `WithSwitch` |
| `MakeExprStmt(expr)` | Expression statement |
| `MakeLocalVar(name, ty)` | Named local variable |
| `MakeLocalVarUnique(ty)` | Hygienic temporary local |
| `MakeWhile(cond, body)` | While loop |
| `MakeFor(init, cond, inc, body)` | For loop |
| `MakeDoWhile(body, cond)` | Do-while loop |
| `WithLoop(loop) { ... }` | Set `loop` as the current loop context for the block so a `Quote()` template built inside it resolves `break`/`continue` against `loop` |
| `LoopSetBody(loop, body)` | Set `loop`'s body; expands a `QuoteLazy()` fragment under `loop`'s own `break`/`continue` targets |

### Function Builder APIs

| Convenience macro | Description |
|-------------------|-------------|
| `PublishNode(obj_or_type)` | Publish a generated function/global in current scope; accepted as a no-op for generated types |
| `PublishNodeAt(obj_or_type, tok)` | Publish with an explicit diagnostic token |
| `MakeFunction(name, ret_type)` | Create a function object |
| `$forward_declare(fn)` | Deprecated alias for `PublishNode(fn)` |
| `FunctionAddParam(fn, name, ty)` | Add function parameter |
| `FunctionSetBody(fn, body)` | Set function body |
| `FunctionSetStatic(fn, flag)` | Set static linkage |
| `FunctionSetInline(fn, flag)` | Set inline flag |
| `FunctionSetVariadic(fn, flag)` | Set variadic flag |
| `WithFn(fn) { ... }` | Set `fn` as the current function context for the block so `Quote("return x;")` applies the correct return-type cast |
| `WithBlock(block) { ... }` | Set `block` as the current block context for `BlockAddStmt(stmt)` |

### Aggregate And Switch Builder Contexts

Scoped builder helpers let macros group child additions without repeating the
parent pointer at every call site. The explicit forms remain available.

```c
[[cccc::comptime]]
void generate_point(void) {
    Type *int_ty = GetType("int");
    Type *point = MakeStruct("Point");
    WithStruct(point) {
        StructAddField("x", int_ty);
        StructAddField("y", int_ty);
    }
}
generate_point();
```

```c
[[cccc::comptime]]
void generate_switch(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("classify", int_ty);
    FunctionAddParam(fn, "x", int_ty);

    WithFn(fn) {
        Node *sw = MakeSwitch(MakeParamRef(fn, "x"));
        WithSwitch(sw) {
            SwitchAddCase(MakeIntLiteral(0), MakeReturn(MakeIntLiteral(1)));
            SwitchSetDefault(MakeReturn(MakeIntLiteral(-1)));
        }
        FunctionSetBody(fn, sw);
    }
}
generate_switch();
```

`WithEnum(e)` similarly enables `EnumAddConstant(name, value)`, and
`WithBlock(block)` enables `BlockAddStmt(stmt)`.

`SwitchAddCase`'s `value` must be a compile-time constant expression (it is
constant-folded the same way a hand-written `case` label is). A case value
that collides with one already added to the same switch, or a second
`SwitchSetDefault` call on the same switch, is a compile error -- matching the
diagnostics a hand-written `switch`/`case`/`default` gets for duplicate or
overlapping case values and multiple default labels.

### Global Variable Builder APIs

| Convenience macro | Description |
|-------------------|-------------|
| `GlobalVar(name, ty)` | Create a global variable definition |
| `GlobalVarSetInitData(var, data, len)` | Set raw initial data (`len` must equal `ty->size`) |
| `GlobalVarSetStatic(var, flag)` | Set internal linkage (file-scope `static`) |

### Node Kinds

Use these constants with `MakeBinary()` and `MakeUnary()`:

```c
NK_ADD, NK_SUB, NK_MUL, NK_DIV, NK_MOD, NK_NEG
NK_BITAND, NK_BITOR, NK_BITXOR, NK_BITNOT, NK_SHL, NK_SHR
NK_EQ, NK_NE, NK_LT, NK_LE
NK_NOT, NK_LOGAND, NK_LOGOR
NK_ASSIGN, NK_ADDR, NK_DEREF, NK_COMMA
```

## Attribute Syntax

Both C23 attribute syntax and GNU attribute syntax are accepted everywhere:

| C23 form | GNU form |
|----------|----------|
| `[[cccc::comptime]]` | `__attribute__((comptime))` |

The canonical form used in this document and in CCCC examples is `[[cccc::comptime]]`.

## Constraints

- Macro declarations accept at most 8 fixed parameters. A trailing `...`
  variadic tail can receive additional call-site arguments.
- Macro code runs at compile time and cannot inspect runtime values.
- Global-generation macros compile before the main parse. They can see safe
  file-scope declarations from preprocessed includes and source. An ordinary
  C function the comptime code calls has its body compiled into the macro VM
  on demand, as long as that function is *defined* in one of the source files
  cccc is given; a function that is only declared (its definition compiled
  and linked separately, or missing entirely) still cannot be called from
  comptime code, and a body that fails to compile in the isolated comptime
  context is a compile error. A constant-initialized global declared in the
  main source file is visible (its snapshot value, not a live reference — see
  [Pre-parse macro declaration context](macros-execution.md#what-comptime-code-can-see));
  any other initialized global is not.
- Global-generation macros run before the main parse, so generated definitions
  are visible everywhere. Inline macros expand at the call site and follow
  normal C declaration rules for any side-effect definitions they emit.
- `PublishNode(obj)` publishes generated functions and globals in the current
  parser scope. Generated struct/union/enum/typedef types already self-publish,
  and `PublishNode(type)` is accepted as a no-op.
- `void` macros cannot be used in expression position; doing so is a compile
  error.

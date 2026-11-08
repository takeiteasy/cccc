# Walking function bodies

Comptime code reads the typed AST of any function definition with a set of
read-only accessors. Use them for linters, checkers, binding generators and
source-to-source tools written as comptime libraries.

```c
[[cccc::comptime]]
Node *count_statements(void) {
    Obj *fn = FindGlobal("work");
    return MakeIntLiteral(BlockCount(FnBody(fn)));
}
```

Every accessor accepts `NULL` and a node of the wrong kind, and answers `NULL`
or `0` (`AtomicOrder` and `AtomicFailOrder` answer -1). Walking never changes
the tree.

## Get a function

| Task | API |
|------|-----|
| Find a function | `FindGlobal(name)`, `^^name`, `AttrTargetObj(target)` |
| Body block (`NULL` for a declaration) | `FnBody(fn)` |
| Parameters, in declaration order | `FnParamCount(fn)`, `FnParamAt(fn, i)` |
| Locals, in declaration order | `FnLocalCount(fn)`, `FnLocalAt(fn, i)` |
| Step through a list of `Obj` | `ObjNext(obj)` |

`FnParamAt` and `FnLocalAt` return an `Obj`; read it with `ObjName` and
`ObjType`. Locals are the variables written in the source, from every nested
block; parameters and compiler temporaries are left out.

## Walk nodes

| Task | API |
|------|-----|
| Kind and type | `GetNodeKind(n)` returns an `NK_*` value; `GetNodeType(n)` returns the resolved `Type *` |
| Statements of a block | `BlockCount(n)`, `BlockAt(n, i)`, or `NodeBody(n)` then `NodeNext` |
| Operands | `NodeLhs`, `NodeRhs`, `NodeCond`, `NodeThen`, `NodeElse`, `NodeInit`, `NodeInc` |
| Calls | `CallCallee(n)`, `CallArgCount(n)`, `CallArgAt(n, i)` |
| Literals | `NodeIntValue(n)`, `NodeFloatValue(n)` |
| Variables and members | `NodeVarObj(n)`, `NodeMember(n)` |
| Switch | `SwitchCaseCount(n)`, `SwitchCaseAt(n, i)`, `SwitchDefault(n)`, `CaseBegin(c)`, `CaseEnd(c)`, `CaseBody(c)` |
| Goto, break, continue, labels | `LabelName(n)`, `GotoJumpKind(n)` |
| Inline asm | `AsmTemplate(n)`, `AsmOperandConstraint(n, i)`, `AsmClobberAt(n, i)` and more ([asm](#jumps-asm-atomics-and-bit-operations)) |
| Atomics and fences | `AtomicOrder(n)`, `AtomicIsWeak(n)`, `CasAddr(n)`, `FenceIsSignal(n)` |
| Bit operations, checked arithmetic | `BitopOp(n)`, `BitopWidth(n)`, `OverflowOp(n)`, `OverflowResultPtr(n)` |
| Comptime calls not yet expanded | `MacroCallName(n)`, `MacroCallArgCount(n)`, `MacroCallArgAt(n, i)` |
| Drop implicit conversions | `StripImplicit(n)` |

`NodeNext(n)` is the next sibling in a statement or argument list. Walking a
list with `NodeNext` is linear; `BlockAt` and `CallArgAt` start from the
front each time.

Which operands each kind uses:

| Kind | Operands |
|------|----------|
| `NK_ADD` ... `NK_LOGOR`, `NK_ASSIGN`, `NK_COMMA` | `NodeLhs`, `NodeRhs` |
| `NK_NEG`, `NK_NOT`, `NK_BITNOT`, `NK_ADDR`, `NK_DEREF`, `NK_CAST` | `NodeLhs` |
| `NK_MEMBER` | `NodeLhs` (the struct expression), `NodeMember` |
| `NK_COND` (`?:`), `NK_IF` | `NodeCond`, `NodeThen`, `NodeElse` |
| `NK_FOR` (also `while`) | `NodeInit`, `NodeCond`, `NodeInc`, `NodeThen` (the body) |
| `NK_DO` | `NodeCond`, `NodeThen` |
| `NK_SWITCH` | `NodeCond`, `NodeThen` (the body block), the case accessors |
| `NK_CASE` | `CaseBegin`, `CaseEnd`, `CaseBody` |
| `NK_RETURN`, `NK_EXPR_STMT` | `NodeLhs` |
| `NK_FUNCALL` | the call accessors |
| `NK_BLOCK`, `NK_STMT_EXPR` | the block accessors |
| `NK_VAR` | `NodeVarObj` |
| `NK_NUM` | `NodeIntValue` or `NodeFloatValue`, by `GetNodeType` |
| `NK_GOTO` | `LabelName` (`NULL` for `break` and `continue`), `GotoJumpKind` |
| `NK_LABEL` | `LabelName`, `NodeLhs` (the labelled statement) |
| `NK_LABEL_VAL` (`&&name`) | `LabelName` |
| `NK_GOTO_EXPR` (`goto *p`) | `NodeLhs` (the target address) |
| `NK_ASM` | the asm accessors |
| `NK_ALOAD`, `NK_ASTORE`, `NK_EXCH` | `NodeLhs` (the address), `NodeRhs` (the value, not for loads), `AtomicOrder` |
| `NK_CAS` | `CasAddr`, `CasExpected`, `CasDesired`, `AtomicOrder`, `AtomicFailOrder`, `AtomicIsWeak` |
| `NK_FENCE` | `NodeLhs` (the order), `FenceIsSignal` |
| `NK_BITOP` | `NodeLhs` (the argument), `BitopOp`, `BitopWidth` |
| `NK_OVERFLOW_ARITH` | `NodeLhs`, `NodeRhs`, `OverflowResultPtr`, `OverflowOp` |

`SwitchCaseAt` lists cases in source order. A `case 5 ... 9:` range has
different `CaseBegin` and `CaseEnd`; a default label comes from
`SwitchDefault`, not the case list.

## The tree is what the compiler built

A walker sees the tree code generation sees, not the source text.

| Source | Tree |
|--------|------|
| `int x = a + 1;` | a block holding an empty statement and the assignment |
| `a + b` with mixed types | `NK_CAST` nodes around the operands |
| a function that falls off the end | an added `return` |
| `for (int i = ...)` | the declaration inside the `NK_FOR` init |

`StripImplicit(n)` peels the casts the compiler inserted and keeps a cast
written in the source. It also unwraps a call whose arguments were hoisted
into temporaries, so `CallCallee` and `CallArgAt` work on either form.[^wrap]

## Jumps, asm, atomics and bit operations

`break` and `continue` are `NK_GOTO` nodes with no label name:

```c
if (GetNodeKind(n) == NK_GOTO) {
    switch (GotoJumpKind(n)) {
        case AST_JUMP_GOTO:     /* LabelName(n) is the target */ break;
        case AST_JUMP_BREAK:    break;
        case AST_JUMP_CONTINUE: break;
    }
}
```

Inline asm operands are numbered outputs first, then inputs. An output written
`+r` counts as an output.

| Task | API |
|------|-----|
| Template | `AsmTemplate(n)` |
| Counts | `AsmOutputCount(n)`, `AsmInputCount(n)`, `AsmClobberCount(n)`, `AsmGotoLabelCount(n)` |
| Operand `i` | `AsmOperandConstraint(n, i)`, `AsmOperandName(n, i)` (`NULL` when unnamed), `AsmOperandExpr(n, i)` |
| Clobber `i`, `asm goto` label `i` | `AsmClobberAt(n, i)`, `AsmGotoLabelAt(n, i)` |
| Qualifiers | `AsmIsVolatile(n)`, `AsmIsGoto(n)` |

`AtomicOrder` and `AtomicFailOrder` return the `__ATOMIC_*` value, 0 (relaxed)
to 5 (seq_cst). A memory order that is not a constant, and a builtin without
one such as `__builtin_atomic_load`, read as 5. A node of any other kind
returns -1, because 0 is a valid order.

`BitopOp` returns an `AST_BITOP_*` value and `OverflowOp` an `AST_OVERFLOW_*`
value; both return 0 for another kind.

| `BitopOp` | `BitopWidth` |
|-----------|--------------|
| `AST_BITOP_CLZ`, `_CTZ`, `_FFS` | 32 for `__builtin_clz`, 64 for `__builtin_clzll` |
| `AST_BITOP_BSWAP` | 16, 32 or 64 |
| `AST_BITOP_POPCOUNT`, `_PARITY` | 0 (the width of the argument is `GetNodeType(NodeLhs(n))`) |

`AST_OVERFLOW_ADD`, `_SUB` and `_MUL` are `__builtin_add_overflow` and its two
siblings; the result pointer is the third argument.

## Comptime calls inside a body

An attribute handler runs right after the function body is parsed, before the
comptime calls in it are expanded. Each such call is an `NK_MACRO_CALL` node
with a name and arguments.

```c
@comptime(attribute("inspect")) void inspect(AttrTarget *target) {
    Node *ret = BlockAt(FnBody(AttrTargetObj(target)), 0);
    Node *call = StripImplicit(NodeLhs(ret));   // answer(42)
    // GetNodeKind(call) == NK_MACRO_CALL, MacroCallName(call) is "answer"
}
```

A comptime call in expression position runs after parsing. It sees every
function body in the file, including functions defined later, with all
comptime calls but its own still unexpanded.

A comptime call at file scope can run before the function it names is parsed.
`FindGlobal` then returns the declaration: `ObjIsDefinition` is false and
`FnBody` is `NULL`. Put the call after the function, or walk from an attribute
handler.

## Example: a lint

A handler that rejects `==` and `!=` on floating-point values:

```c
[[cccc::comptime]] void check(Node *n);
[[cccc::comptime]] void check(Node *n) {
    if (!n)
        return;
    NodeKind k = GetNodeKind(n);
    if (k == NK_BLOCK || k == NK_STMT_EXPR) {
        for (int i = 0; i < BlockCount(n); i++)
            check(BlockAt(n, i));
        return;
    }
    if (k == NK_EQ || k == NK_NE) {
        Type *t = GetNodeType(StripImplicit(NodeLhs(n)));
        if (t && (GetTypeKind(t) == TK_DOUBLE || GetTypeKind(t) == TK_FLOAT))
            MacroErrorAt(0, "floating-point comparison is an error: use a tolerance");
    }
    check(NodeCond(n));
    check(NodeThen(n));
    check(NodeElse(n));
    check(NodeLhs(n));
    check(NodeRhs(n));
}

@comptime(attribute("no_float_eq")) void no_float_eq(AttrTarget *target) {
    check(FnBody(AttrTargetObj(target)));
}

@no_float_eq int same(double a, double b) {
    return a == b;   // compile error: use a tolerance
}
```

The helper is a comptime function too, so it can name `Node`. A plain
`#define` belongs to the runtime code; write `#define @comptime NAME` for a
macro that comptime code uses ([routing directives](macros-emit.md#routing-directives)).

[^wrap]: With checked pointers, a call whose checked arguments need temporaries
    is an `NK_COMMA` node holding the temporaries and the call.

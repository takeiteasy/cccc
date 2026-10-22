# Compile-Time Macros

Compile-time macros are C functions that CCCC compiles and runs during
compilation. They can inspect compile-time types, build AST nodes, generate
functions and global variables, and replace macro call sites with generated code.

A worked, real-world use of the comptime pass — a small Lisp compiler that
runs entirely inside a comptime function, lowering a `.lisp` source file to
plain generated C — lives in
[`examples/ccccl/`](../examples/ccccl); its README covers the
`-c=generated`/`-c=native` invocation shapes and the AST-building patterns
used to get there.

A macro function is declared by annotating it with `[[cccc::comptime]]` (C23
attribute syntax), `__attribute__((comptime))` (GNU attribute syntax), or the
concise `@comptime` shorthand. The `__comptime` and `__comptime__` keyword
aliases are accepted as the same CCCC-specific marker. All forms are equivalent
and accepted
everywhere:

```c
// All forms are identical:
[[cccc::comptime]]        int helper(int n) { return n * 2; }
__attribute__((comptime)) int helper(int n) { return n * 2; }
@comptime                 int helper(int n) { return n * 2; }
__comptime                int helper(int n) { return n * 2; }

// Expression-position (call-site) macro — returns Node*:
[[cccc::comptime]]  Node *make_val(void) { return MakeIntLiteral(42); }
@comptime           Node *make_val(void) { return MakeIntLiteral(42); }
```

The old `[[cccc::macro]]` / `__attribute__((macro))` aliases (and the `@macro`
shorthand) are no longer accepted — using any of them is a compile error
directing you to `[[cccc::comptime]]`. See the [Attribute Syntax](macros-api.md#attribute-syntax) note below.


## Return-Value Model

A macro's return value is **the node spliced at the call site**, replacing the
invocation. Top-level definitions — functions created with `MakeFunction()`,
globals with `GlobalVar()` — are **side effects** injected regardless of
what the macro returns. How generated names become visible to the parser depends
on which execution form you use.

| Call context | Return value |
|--------------|--------------|
| Expression position (`int x = mac()`) | Must return a non-NULL `Node *` that is itself an **expression** node. NULL is a compile error, and so is a statement-kind return (see [Statement returns in expression position](#statement-returns-in-expression-position) below). |
| Global variable initializer (`int G = mac()`) | Must return a `Node *` that evaluates to an integer or float constant. Pointer/struct results are not supported. See [Comptime functions in global initializers](macros-global.md#comptime-in-global-initializers). |
| Declaration position (file-scope `mac();`) | Returning NULL or `void` is legal — means "I only emitted definitions." |

For definition-only macros, declare the return type `void`. This is
self-documenting and lets you omit the return statement entirely. Using a `void`
macro in expression position is a compile error.

```c
[[cccc::comptime]]
void emit_helpers(void) {
    // build functions, globals — no return needed
}
emit_helpers();
```

`Node *` macros may still return NULL in declaration position without error. The
old `return MakeIntLiteral(0)` idiom still works but is no longer needed.

### Statement returns in expression position

`Quote()` auto-wraps a multi-statement template in braces, so it's easy to end
up with an `ND_BLOCK` (or another statement-kind node — an `if`, a `for`, a
bare `return`, a `switch` or bare `case` label, a `goto` or computed
`goto *p`, an `asm(...)` statement, ...) as a macro's return value:

```c
[[cccc::comptime]]
Node *gen(void) {
    return Quote("int i; i = 3; return i;"); // multi-statement -> ND_BLOCK
}

int r = gen(); // error: comptime function 'gen' returned a statement and
               // cannot be used in expression position
```

Called in *statement* position (`gen();` on its own, at file or block scope)
this is fine — the statement splices in directly. Used in expression position
it is a compile error, because a statement isn't a value the surrounding
expression can consume.

To produce a value from a multi-statement template, wrap it in a GNU
statement expression, `({ ... })`, whose last statement (an expression
statement) supplies the value:

```c
[[cccc::comptime]]
Node *gen(void) {
    return Quote("({ int i; i = 3; i; })"); // ND_STMT_EXPR, value == i
}

int r = gen(); // r == 3
```

Careful: a `return` *inside* `({ ... })` returns from the **enclosing
function**, not from the statement expression — it does not supply the
statement expression's value. Don't reach for `return` when you mean "the
value of this block is..."; end the block in a plain expression statement
instead, as above.

The same rule applies to `QuoteLazy()`/`QuoteLazyN()` fragments spliced in
expression position — see
[Deferred templates with `QuoteLazy`](macros-templates.md#deferred-templates-quotelazy).

A template can hold `_Pragma("omp ...")` to build an OpenMP region; see
[OpenMP](openmp.md#generated-code).

## Guide pages

| Page | Covers |
|---|---|
| [macros-execution.md](macros-execution.md) | When comptime code runs, what it can see, isolation and cross-file calls |
| [macros-attributes.md](macros-attributes.md) | Custom attribute handlers and member attributes |
| [macros-global.md](macros-global.md) | File-scope generation, comptime in initializers and constants |
| [macros-emit.md](macros-emit.md) | Emitting directives, emit blocks, attributes on generated functions |
| [macros-call-site.md](macros-call-site.md) | Call-site expansion with `Node *` macros |
| [macros-helpers.md](macros-helpers.md) | Helper functions and comptime blocks |
| [macros-includes.md](macros-includes.md) | Include routing (`@comptime`, `@shared`) and `@build`/`@test` directives |
| [macros-variables.md](macros-variables.md) | Comptime variables and `constexpr` readback |
| [macros-quoting.md](macros-quoting.md) | `Quote`, `QuoteN` and splicing |
| [macros-templates.md](macros-templates.md) | Templates in generated bodies, `QuoteLazy`, labels |
| [macros-reflection.md](macros-reflection.md) | Type and symbol reflection, `$identifier` |
| [macros-builders.md](macros-builders.md) | Function, global, local and initializer builders |
| [macros-stdlib.md](macros-stdlib.md) | The bundled macro standard library |
| [macros-debugging.md](macros-debugging.md) | Diagnostics, `macroexpand`, profiling |
| [macros-api.md](macros-api.md) | API tables, node kinds, attribute syntax, constraints |

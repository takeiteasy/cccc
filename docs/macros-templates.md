# Templates in Generated Code

Using `Quote` inside generated bodies, deferred templates, loops and labels.

## Function context: `WithFn`

`Quote("return x;")` needs the enclosing function's return type to apply the implicit cast. Wrap the call in `WithFn(fn)` to provide it:

```c
@comptime void generate_answer(void) {
    Obj *fn = MakeFunction("answer", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(fn, Quote("return 42;"));
    }
}
generate_answer();
```

| Situation | `WithFn` |
|---|---|
| Template produces a `return` | Needed, for the return-type cast |
| `FunctionSetBody` called at file scope | Locals declared in the body (`int x = 1;` in a template, `MakeLocalVar`, `MakeCompoundLiteral`) attach to the function without it |
| Comptime macro running inside another function's body, building a different function | Required: otherwise the locals cannot be told apart from the caller's, and `FunctionSetBody` reports a compile error naming the missing wrapper[^with] |

```c
@comptime Node *gen(void) {
    Obj *fn = MakeFunction("helper", GetType("int"));
    WithFn(fn) {   // required: gen() is running inside main()
        FunctionSetBody(fn, Quote("{ int a = 40; int b = 2; return a + b; }"));
    }
    PublishNode(fn);
    return Quote("0");
}
int helper(void);
int main(void) {
    int y = gen();
    return helper() + y;
}
```

## Deferred templates: `QuoteLazy`

`Quote` parses its template immediately, so a fragment is validated against the scope at the call, not the loop it is later spliced into:

```c
Node *body = Quote("if (i == 3) break;");   // fails: no loop, no `i` yet
```

`QuoteLazy(tmpl, ...)` and `QuoteLazyN(tmpl, nodes, count)` capture the text and parse it only when spliced, in the splice site's scope and loop labels:

```c
@comptime void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        Node *body = QuoteLazy("if (i == 3) break;");
        Node *loop = Quote("int i; for (i = 0; i < 10; i++) { $1; } return i;", body);
        FunctionSetBody(fn, loop);
    }
}
gen();

int main(void) { return f() == 3 ? 42 : 1; }
```

| Behaviour | Detail |
|---|---|
| Splice position | Statement or expression `$N`. A fragment that parses to a statement (`goto`, `goto *p`, a `case` label, `asm`, several statements) in expression position is an error; wrap it in `({ ... })` ([macros.md](macros.md#statement-returns-in-expression-position)) |
| Lazy in lazy | Allowed |
| Spliced twice | Each splice re-parses: separate `break` targets and locals. A local declared by the fragment is declared twice in one scope; use `Gensym` |
| As a `$@N` list splice | Rejected: `$@N` needs a parsed node chain (`NodeList()`/`Quote()`) |
| Never spliced | Error: "QuoteLazy fragment was never spliced" |

## `break` and `continue` in builder loops

`MakeWhile`, `MakeFor` and `MakeDoWhile` give the loop its own `break`/`continue` targets. A `QuoteLazy` body passed to one binds correctly:

```c
Node *loop = MakeWhile(MakeIntLiteral(1), QuoteLazy("{ break; }"));
```

An eager `Quote` body would already be parsed, with `break` bound to the surrounding loop, before the builder runs (C evaluates arguments first):

```c
Node *loop = MakeWhile(MakeIntLiteral(1), Quote("{ break; }"));   // stray break
```

Build the loop with a `NULL` body, then attach an eager template inside `WithLoop`:

```c
Node *loop = MakeWhile(MakeIntLiteral(1), NULL);
WithLoop(loop) {
    LoopSetBody(loop, Quote("{ break; }"));
}
```

`WithLoop` blocks nest with each other and with loops inside templates; each loop's targets are visible only within its own block.

## Labels in templates

`goto`, named labels, label addresses (`&&done`) and computed `goto *p` work inside `Quote`/`QuoteLazy` templates. Labels are **hygienic**:

| Rule | Example |
|---|---|
| A reference and a target in the same template bind, in any order | `&&fwd` before `fwd:` |
| Two expansions of one template get private copies | A `goto` in one never lands in the other |
| A `goto` whose target the template does not define resolves against the enclosing function | `Quote("goto cleanup;")` jumps to the function's `cleanup:` |
| The reverse does not work | The function cannot `goto` a label only a template defines: `use of undeclared label` |
| A `goto` into a scope holding `__attribute__((cleanup))` variables across the template boundary | Diagnosed |

A function label whose only `goto` comes from a spliced template counts as used, so `-Wunused` decides after expansion. An unresolved label reaching code generation is an internal error, never silently wrong code.

[^with]: Without the wrapper, the generated function's locals would be built while the caller is the active function context.

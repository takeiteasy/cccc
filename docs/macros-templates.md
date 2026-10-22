# Templates in Generated Code

Using `Quote` inside generated bodies, deferred templates and labels.

### `Quote` inside generated function bodies

`Quote("return x;")` needs to know the enclosing function's return type to
apply the correct implicit cast. When building a generated function body, wrap
the quote call in `WithFn(fn)` to establish that context:

```c
[[cccc::comptime]]
void generate_answer(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("answer", int_ty);
    WithFn(fn) {
        FunctionSetBody(fn, Quote("return 42;"));
    }
}
generate_answer();
```

Without `WithFn`, `Quote("return x;")` at file scope (where there is no
enclosing function) still compiles, but the implicit return-type cast is
skipped. For macros that only use `MakeReturn(MakeIntLiteral(...))` directly
this does not matter; it matters when the template produces a `return`
statement.

`FunctionSetBody` attaches locals declared inside the body — including
`int x = 1;`-style declarations inside a `Quote()` template, and any local
created by `MakeLocalVar`/`MakeCompoundLiteral` while the body is being built
— to the function they were built for, whether or not that construction
happened inside `WithFn`, **as long as `FunctionSetBody` is called from
file scope** (no comptime macro is currently executing on behalf of another
function). `WithFn` is still required for the return-type cast above, and
it's the only way to target `MakeLocalVar` at a specific function from
outside a `Quote()` template, but it is no longer required just to give a
top-level generated function's own locals a home.

If a comptime macro is itself invoked from *inside* an ordinary function
(an inline macro call, or a `[[cccc::comptime]]` function called from a
normal function body) and, from there, calls
`MakeFunction()`+`FunctionSetBody(fn2, ...)` on some *other* function `fn2`,
`WithFn(fn2)` is **required** around that call:

```c
[[cccc::comptime]]
Node *gen(void) {
    Obj *fn = MakeFunction("helper", GetType("int"));
    WithFn(fn) { // required here -- gen() is running inside main()
        FunctionSetBody(fn, Quote("{ int a = 40; int b = 2; return a + b; }"));
    }
    PublishNode(fn);
    return Quote("0");
}
int helper(void);
int main(void) {
    int y = gen(); // gen() runs from inside main()'s own body
    return helper() + y;
}
```

Without `WithFn(fn2)` here, `fn2`'s locals would be built while the calling
function (`main`, above) is the active function context, and there is no
reliable way to tell them apart from `main`'s own locals afterward —
`FunctionSetBody` detects this and raises a compile error naming the
required `WithFn(fn2)` wrapper rather than silently misattaching them (which
previously produced broken generated C, or a genuine frame-slot aliasing
hazard depending on the two functions' relative layout).

### Deferred templates with `QuoteLazy`

`Quote(tmpl, ...)` tokenizes and parses its template **immediately**, at the
point of the call — so a fragment built by its own `Quote()` call is
validated (`break`/`continue`, variable scope) against whatever's in scope
*right then*, not against the loop or block it is later spliced into. That
makes a natural comptime pattern fail: building a loop body once and
composing it into a `for`/`while` template built separately.

```c
[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        Node *body = Quote("if (i == 3) break;"); // fails: no loop yet, no `i`
        Node *loop = Quote(
            "int i; for (i = 0; i < 10; i++) { $1; } return i;", body);
        FunctionSetBody(fn, loop);
    }
}
```

`QuoteLazy(tmpl, ...)` / `QuoteLazyN(tmpl, nodes, count)` capture the
template text and splice arguments **without parsing them**. The template is
only tokenized and parsed once it is actually spliced in — at a `$N` splice
point inside another `Quote()`/`QuoteLazy()` template, or passed directly to
`FunctionSetBody` — at that splice site's own scope and loop labels. Simply
replacing the inner `Quote()` with `QuoteLazy()` above makes it work:

```c
[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    WithFn(fn) {
        Node *body = QuoteLazy("if (i == 3) break;");
        Node *loop = Quote(
            "int i; for (i = 0; i < 10; i++) { $1; } return i;", body);
        FunctionSetBody(fn, loop);
    }
}
gen();

int main(void) {
    return f() == 3 ? 42 : 1;
}
```

A `QuoteLazy()` fragment splices in either statement or expression position,
exactly like an ordinary `$N` — but if the fragment parses to a statement
(a multi-statement template, or a single statement such as a `goto`,
computed `goto *p`, bare `case` label, or `asm(...)`) and is spliced at an
expression-position `$N`, that's a compile error ("fragment '...' is a
statement and cannot be spliced in expression position"), the same rule and
the same fix (a GNU
statement expression, `({ ... })`) as for an eagerly-`Quote()`d template —
see [Statement returns in expression position](macros.md#statement-returns-in-expression-position).
It can also splice another `QuoteLazy()`
fragment (lazy-in-lazy composition), and the same fragment can be spliced
more than once — each splice re-parses it independently, so two loops built
from the same `QuoteLazy()` result get their own, unrelated `break` targets
and locals (a fragment that declares a local, spliced twice into the same
scope, declares it twice — use `Gensym` if that matters).

A `QuoteLazy()` result is **not** valid as a `$@N` list-splice argument —
`$@N` expects an already-parsed `->next`-linked node chain
(`NodeList()`/`Quote()`), not a deferred template — and is rejected as soon
as it's registered. Build the chain with ordinary `Quote()`/`NodeList()`
elements; a `QuoteLazy()` fragment can still appear alongside such a chain in
the same template, as an ordinary `$N`.

A `QuoteLazy()` result that is never spliced into anything — returned
directly into expression position, say — is a compile-time error ("QuoteLazy
fragment was never spliced") rather than reaching codegen with a meaningless
type.

### `break`/`continue` in a builder loop's body

`MakeWhile`, `MakeFor`, and `MakeDoWhile` each give the loop node its own
`break`/`continue` targets, the same way the parser does for an ordinary
`while`/`for`/`do`. A `QuoteLazy()` body passed straight to one of these
builders is expanded under those targets, so `break`/`continue` inside it
bind correctly:

```c
Node *loop = MakeWhile(MakeIntLiteral(1), QuoteLazy("{ break; }"));
```

An **eager** `Quote()` body cannot work the same way, for the same reason a
`QuoteLazy()`-only fix couldn't cover the whole picture: C evaluates a
function call's arguments before the call runs, so `Quote("{ break; }")` is
already fully parsed — and its `break` already bound to whatever loop was
in scope at the call site — before `MakeWhile` ever runs to create the new
loop and its labels:

```c
Node *loop = MakeWhile(MakeIntLiteral(1), Quote("{ break; }")); // stray break
```

`WithLoop(loop) { ... }` / `LoopSetBody(loop, body)` solve this the way
`WithFn`/`FunctionSetBody` solve the equivalent problem for function bodies:
build the loop with a `NULL` body first, then attach an eager `Quote()`
template from *inside* a `WithLoop` block, so its `break`/`continue` resolve
against the loop's targets as it's parsed:

```c
Node *loop = MakeWhile(MakeIntLiteral(1), NULL);
WithLoop(loop) {
    LoopSetBody(loop, Quote("{ break; }"));
}
```

`WithLoop` blocks nest correctly with loops built inside a `Quote()`
template and with builder loops nested inside each other — each loop's
`break`/`continue` targets are only visible for the duration of its own
`WithLoop` block (or its own `QuoteLazy()` body expansion).

### Labels inside templates

`goto`, a named label (`done:`), a label address (`&&done`), and a computed
`goto *p` all work inside a `Quote()`/`QuoteLazy()` template. A template's
labels are **hygienic** — private to that template:

- A reference and its target defined in the **same** template bind to each
  other, regardless of order (`&&fwd` before `fwd:` is fine).
- Two expansions of one label-bearing template in the same function each get
  their own private copy of the label; a `goto` in one expansion never lands
  in the other.
- A `goto` (or `&&label`) in a template whose target the template does *not*
  define is a free reference, resolved after expansion against a label the
  **enclosing function** defines. `Quote("goto cleanup;")` spliced into a
  function with a `cleanup:` label jumps there.
- The reverse does **not** work: the enclosing function cannot `goto` a label
  that only a template defines — that is `use of undeclared label`, matching
  the diagnostic an eager `Quote()` has always produced for this shape.
- A `goto` that a template contributes and that lands on a label in a scope
  holding `__attribute__((cleanup))` variables is diagnosed: the exited-scope
  set is not computed for a jump that crosses the template boundary.

A still-unresolved label reference reaching code generation is an internal
error, not silently-wrong code.

A function label whose only `goto` comes from a spliced template counts as
used: `-Wunused` decides "unused label" only after templates are expanded and
their free references bound.

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

## Template instances in `Quote`

A `Quote` or `QuoteLazy` template can name [template](templates.md) instances with `@inst`. The instance is created when the template is parsed and emitted with the program, whether it is a function, struct, union, enum or typedef.

```c
@template(T) T max(T a, T b) { return a > b ? a : b; }

@comptime Node *biggest(Node *a, Node *b) {
    return Quote("@inst(int) max($1, $2)", a, b);   // calls max__int
}

int main(void) { return biggest(40, 42); }
```

## Templates in comptime code

Comptime code has its own [templates](templates.md): a template defined for the comptime program is used only by it, and a runtime template is not visible to comptime code, so both can use the same name. Define one in a comptime block or in an `@shared` header, which both programs read.

```c
#pragma cccc comptime begin
@template(T) T twice(T v) { return v * 2; }

int ct_answer(void) { return @inst(int) twice(21); }
#pragma cccc comptime end
```

| Where | Behavior |
|---|---|
| `@template`, `@specialize`, `@inst` in a comptime function, a comptime block or an `@shared` header | Work as in runtime code |
| `@inst` as the first token of a declaration in a comptime block | Error: use it inside a function |
| `@inst` in comptime code of a runtime template | `'twice' is not a template in comptime code` |

## Instantiating from a handler

`InstantiateType(name, args)` and `InstantiateFunction(name, args)` instantiate a runtime template from a handler. `args` is the text written inside `@inst(...)`, so it takes types, constants and packs. Each call returns the instance's `Type *` or `Obj *`.

```c
@template(T, int N) struct Arr { T v[N]; };
@template(T) T maxof(T a, T b) { return a > b ? a : b; }

@comptime Node *bytes(void) {
    Type *arr = InstantiateType("Arr", "char, 5");
    return MakeIntLiteral(TypeSize(arr));                // 5
}

@comptime Node *biggest(Node *a, Node *b) {
    Obj *fn = InstantiateFunction("maxof", "int");       // maxof__int
    return Quote("maxof__int($1, $2)", a, b);
}
```

| Case | Result |
|---|---|
| `InstantiateType` on a function template, or `InstantiateFunction` on any other | Error naming the other builtin |
| Trailing text after the arguments (`"int) junk ("`) | Error |
| A call at file scope (`gen();`) | `'Box' is not a template`: it runs before the file is parsed, so no template exists yet. Handlers that run while a function body or attribute is expanded can instantiate[^filescope] |

[^filescope]: `Quote` with `@inst` has the same limit.

[^with]: Without the wrapper, the generated function's locals would be built while the caller is the active function context.

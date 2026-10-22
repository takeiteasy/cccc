# Global Generation

File-scope comptime calls that emit functions, globals and types, and comptime calls inside constants. Emitting preprocessor directives and attributes is in [macros-emit.md](macros-emit.md).

## Generating declarations

A comptime function called at file scope runs before the main parse. CCCC forward-declares every generated function and `extern`-declares every generated global, so nothing needs publishing by hand.

```c
@comptime void generate_answer(void) {
    Obj *fn = MakeFunction("answer", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
}
generate_answer();

int main(void) { return answer(); }
```

In `-c=generated` and `-m` output each generated function gets a prototype ahead of any other generated function that calls it, so mutual recursion compiles under a plain `cc`. A `FunctionPrototype()` with no body is emitted as a bare declaration.

### Arguments

Fixed parameters receive their argument tokens stringified as `char *`: a string literal passes its value, anything else its spelling. A trailing `...` takes any number of further arguments, read with `VarargCount()` and `VarargStrAt(i)`.

```c
@comptime void gen_prefixed(char *prefix, ...) {
    for (int i = 0; i < VarargCount(); i++) {
        const char *name = VarargStrAt(i);   // prefix is "api"; name is read, write, close
    }
}
gen_prefixed("api", read, write, close);
```

Fixed parameters past the eighth spill to the stack. Typical uses are enum-conversion helpers and serializers.

### Returning a block

Returning a block unwraps it and splices its declarations into file scope:

```c
@comptime Node *emit_widget_helpers(void) {
    return Quote("{ struct Widget { int x; int y; };"
                 "  void widget_init(struct Widget *w) { w->x = 0; w->y = 0; } }");
}
emit_widget_helpers();

int main(void) {
    struct Widget w;
    widget_init(&w);
    return w.x == 0 ? 42 : 1;
}
```

| Style | Use when |
|---|---|
| Side effects (`MakeFunction`, ...) | Names or types are computed at compile time |
| Block return | The declarations are literal C source |

## Comptime in global initializers

A `Node *`-returning comptime function that yields an integer or float constant can initialize a scalar global. The call is deferred until after the main parse.

```c
@comptime Node *ct_val(void)                { return MakeIntLiteral(42); }
@comptime Node *ct_add(Node *a, Node *b)    { return MakeBinary(NK_ADD, a, b); }

int G = ct_val();           // 42
int H = ct_add(20, 22);     // 42
float F = ct_val();         // 42.0
int I = ct_add(1, 1) + 40;  // 42
```

| Limit | Result |
|---|---|
| Pointer or struct result | "not a compile-time constant" |
| `constexpr` variable | Not allowed; use a plain global |
| Function returning `int` or `void` | "expression-position comptime functions must return Node*" |

## Comptime in constant expressions

The parser expands a `Node *` comptime call wherever it needs a constant: `case` labels (including ranges), enumerator values, `_Static_assert` conditions and file-scope array lengths.

```c
@comptime Node *PLUS(Node *a, Node *b) { return MakeBinary(NK_ADD, a, b); }

enum { A = PLUS(1, 2) };
int table[PLUS(2, 3)];
_Static_assert(PLUS(40, 2) == 42, "ok");
switch (v) { case PLUS(20, 2): ... }
```

The comptime program is compiled at the first such use. A block-scope length like `int v[PLUS(n, 1)]` stays a runtime VLA.

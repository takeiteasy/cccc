# Quasi-Quoting

Build AST fragments from C source templates.

## Quasi-Quoting

Backtick quasi-quotes embed a C expression or statement directly in a
comptime function. `${...}` evaluates a comptime expression that returns a
`Node *` and splices that node into the template:

```c
[[cccc::comptime]]
Node *add_one(Node *x) {
    return `return ${x} + 1;`;
}

int answer(void) {
    add_one(41);
}
```

Backticks are raw and may span lines, so C string quotes and ordinary
backslashes do not need another layer of escaping. `\`` inserts a literal
backtick; all other backslashes are preserved. Interpolation expressions use
normal C parsing and preprocessing, including commas and nested braces:

```c
[[cccc::comptime]]
Node *choose_second(Node *a, Node *b) {
    return `return ${ ((Node *[]){ a, b })[1] };`;
}
```

`$identifier` remains the reflect operator inside the generated template, so
for example `` `return sizeof($Point);` `` reflects `Point` when the template
is parsed. Backtick interpolation is scalar and uses the same variadic capacity
as `Quote(...)` (up to 64 splice nodes). Use `Quote(...)` directly for `$N`,
`$$`, or `$@` list splicing, and use `QuoteN(...)` when more splice nodes are
required. Legacy `Quote` placeholders are rejected as backtick template tokens
(text such as `"$1"` inside a C string literal remains ordinary text). Nested
backtick quasi-quotes are not supported.

The two forms use the same quote engine:

| Backtick form | Equivalent `Quote` form |
|---|---|
| `` `return 42;` `` | `Quote("return 42;")` |
| `` `return ${x} + 1;` `` | `Quote("return $1 + 1;", x)` |
| Multiline raw template | Escaped C string template |

`Quote(tmpl, ...)` parses a C expression or statement template and splices
`Node *` values into `$1`, `$2`, and later numbered holes.

```c
[[cccc::comptime]]
Node *square(Node *x) {
    return Quote("($1) * ($1)", x);
}

int main(void) {
    return square(6);
}
```

A template with more than one top-level statement does not need explicit
braces — `Quote("a; b; return c;")` and `Quote("{ a; b; return c; }")` produce
the same block, and every statement is kept either way:

```c
[[cccc::comptime]]
Node *pick_or_default(Node *a, Node *b) {
    return Quote("if (!$1) $1 = $2; return $1;", a, b);
}
```

A `;` inside a nested `(...)`/`[...]`/`{...}` (e.g. a `for (a; b; c)` header)
does not count as a top-level separator. Any tokens left over once the
template has been fully parsed — a genuinely malformed template, not a
missing brace — are a compile error rather than being silently discarded.

Numbered holes can be reused and reordered. Use `$$` for sequential left-to-
right holes when order is enough:

```c
[[cccc::comptime]]
Node *sum2(Node *a, Node *b) {
    return Quote("$$ + $$", a, b);
}
```

Do not mix `$N` and `$$` in one template. Use `QuoteN(tmpl, nodes, count)`
when splice nodes are already in an array.

A `$N`/`$@N` placeholder in **statement-list position** needs its own
trailing `;`, exactly like any other statement: `"{ $1; }"`, not `"{ $1 }"`
— the latter is invalid C for the same reason `{ x }` is, and is reported at
the placeholder itself.

### List splicing with `$@N` and `$@`

`$@k;` in **statement-list position** expands an entire `->next`-linked node
chain into the block, replacing one placeholder with N statements. This is
typed unquote-splicing.

```c
[[cccc::comptime]]
Node *double_inc(Node *x) {
    Node *chain = NodeList((Node*[]){
        Quote("$1 += 1;", x),
        Quote("$1 += 1;", x),
    }, 2);
    return Quote("{ $@1; }", chain);
}

int get_plus_two(int v) {
    double_inc(v);
    return v;  // v + 2
}
```

`$@` is the incremental (sequential) form, parallel to `$$`:

```c
[[cccc::comptime]]
Node *two_increments(Node *a, Node *b) {
    return Quote("{ $@; $@; }",
                  Quote("$1 += 10;", a),
                  Quote("$1 += 20;", b));
}
```

You can mix a scalar `$N` and a list `$@N` in the same template when both are
positional. `NodeList(arr, count)` builds the `->next` chain from an array.
An existing `->next` chain (e.g. `MakeBlock(...)->body`) can also be passed
directly as the splice argument.

### Call-argument splicing

`$@k` can also appear as a **direct argument to a function call** in a `Quote`
template, expanding the chain into the callee's argument list. This works for
both **variadic callees** (functions accepting `...`) and **fixed-arity
callees** (functions with an exact parameter count).

For variadic callees the splice occupies zero or more slots in the variadic
portion:

```c
#include <stdarg.h>

int sum_ints(int count, ...) {
    va_list args; va_start(args, count);
    int s = 0;
    for (int i = 0; i < count; i++) s += va_arg(args, int);
    va_end(args);
    return s;
}

[[cccc::comptime]]
Node *call_sum3(Node *a, Node *b, Node *c) {
    Node *chain = NodeList((Node*[]){ a, b, c }, 3);
    return Quote("sum_ints(3, $@1)", chain); // → sum_ints(3, a, b, c)
}
```

For fixed-arity callees the spliced nodes must produce exactly the right number
of arguments after expansion. Parameter casts are applied post-expansion:

```c
int add3(int a, int b, int c) { return a + b + c; }

[[cccc::comptime]]
Node *call_add3(Node *a, Node *b, Node *c) {
    Node *chain = NodeList((Node*[]){ a, b, c }, 3);
    return Quote("add3($@1)", chain); // → add3(a, b, c)
}
```

Mixing a scalar `$N` with a call-arg splice `$@M` in one template is supported:

```c
return Quote("add3($1, $@2)", first_node, pair_chain);
```

An empty chain (`NodeList` with `count == 0`, which returns NULL) is a valid
splice that inserts zero arguments. Using `$@k` as a sub-expression operand
(e.g. `"foo($@1 + 1)"`) rather than a direct argument remains a compile-time
error. Splicing the wrong number of arguments into a fixed-arity callee is also
a compile-time error.

### Compound-literal initializer-list splice

`$@k` can also appear inside the braces of a compound literal, splicing a node
chain as positional initializers for a struct or array:

```c
[[cccc::comptime]]
Node *make_point(Node *px, Node *py) {
    Node *chain = NodeList((Node*[]){ px, py }, 2);
    return Quote("(struct Point){ $@1 }", chain);
    // → (struct Point){ .x = px, .y = py }  (positional, left-to-right)
}

struct Point p = make_point(10, 32); // p.x == 10, p.y == 32
```

Array compound literals are also supported:

```c
[[cccc::comptime]]
Node *make_arr3(Node *a, Node *b, Node *c) {
    Node *chain = NodeList((Node*[]){ a, b, c }, 3);
    return Quote("(int[3]){ $@1 }", chain);
}
```

The splice can mix with ordinary positional initializer elements. Elements
after the splice are placed after the expanded chain:

```c
return Quote("(struct Triple){ 1, $@1 }", pair_chain);
return Quote("(int[]){ 1, $@1, 4 }", pair_chain);
```

Inferred array length is supported for compound literals such as
`(int[]){ $@1 }` and `(int[]){ 1, $@1, 4 }`.

Initializer splices remain positional; use `InitStruct` for designated or
partial struct/union initialization. A mismatch between the expanded element
count and the number of struct fields or explicit array elements is a
compile-time error.

Mixing a scalar placeholder with a compound-literal splice in one template is
fine:

```c
return Quote("(struct Point){ $@2 }", unused_scalar, chain);
```

# Call-Site Expansion

`Node *` macros that replace the call that invoked them.

## Call-Site Expansion

Use an inline macro for call-site expansion inside expressions or statements.
The call is replaced with the returned AST during macro expansion. Inline macros
**must** return a non-NULL node and cannot be used at file scope.

Two equivalent spellings are accepted:

```c
// Attribute argument form
[[cccc::comptime]]
Node *double_it(Node *value) {
    return MakeBinary(NK_ADD, value, value);
}

// C keyword form (preferred)
[[cccc::comptime]]
inline Node *double_it(Node *value) {
    return MakeBinary(NK_ADD, value, value);
}
```

```c
[[cccc::comptime]]
Node *double_it(Node *value) {
    return MakeBinary(NK_ADD, value, value);
}

int main(void) {
    int x = double_it(21);
    return x;
}
```

Macro arguments are `Node *` pointers to the original argument ASTs. A macro
can reuse, inspect, wrap, or replace those nodes.

Inline macros also support a trailing `...` parameter. Fixed arguments are
passed as normal `Node *` parameters, and the variadic tail is available
through `VarargCount()`, `VarargAt(i)`, and
`VarargAsArray()`. A macro with only `...` is valid:

```c
[[cccc::comptime]]
Node *sum_all(...) {
    Node *acc = VarargAt(0);
    for (int i = 1; i < VarargCount(); i++)
        acc = MakeBinary(NK_ADD, acc, VarargAt(i));
    return acc;
}

int x = sum_all(1, 2, 3, 4);
```

Use `VarargAsArray()` when forwarding the tail to an array-form builder
such as `MakeFuncCall(callee, args, n)`:

```c
[[cccc::comptime]]
Node *forward_call(Node *fn_node, ...) {
    return MakeFuncCall(fn_node,
                    VarargAsArray(),
                    VarargCount());
}

int x = forward_call(target_fn, 1, 2, 3);
```

`VarargAsArray()` returns a borrowed read-only `Node **` slice that
is valid only for the current macro call. It returns `NULL` when the variadic
tail is empty. Forwarding shares AST nodes; it does not consume or clone them.
Static macro-to-macro forwarding is handled with `Quote` and splice syntax.
Dynamic macro-call construction is not part of this API.

`VarargAt(i)` and `VarargStrAt(i)` use zero-based indexes.
Variadic helpers report a compile-time error when an index is negative, out of
range, or the helper is used with the wrong macro execution form.

Call-site expansion happens after the containing function body has already been
parsed. If an inline macro creates a separate top-level function or global, the
same parsed code can name that object only if normal C declaration rules are
satisfied. Use a global-generation macro when you want CCCC to publish generated
declarations automatically.

Comptime functions can use ordinary static data tables for lookup work,
including pointer arrays and struct arrays that point at string literals or
other static data:

```c
[[cccc::comptime]]
const char *lookup(const char *name) {
    static const char *headers[] = {"stdio.h", "string.h"};
    return strcmp(name, headers[0]) == 0 ? headers[0] : 0;
}
```

Static initializer tables can also store pointers to other comptime functions.
Entries resolve to callable macro VM function addresses:

```c
[[cccc::comptime]]
int handler_a(void) { return 10; }

[[cccc::comptime]]
int handler_b(void) { return 20; }

[[cccc::comptime]]
int dispatch(int i) {
    static int (*const table[])(void) = {handler_a, handler_b};
    return table[i]();
}
```

Macro expansion is bounded by `--macro-recursion-limit=N` to catch accidental
self-recursive expansions. The default is 256. Set the limit to 0 to disable
the check.

Statement macros work the same way. Return a statement node such as
`MakeReturn(...)`, `MakeIf(...)`, `MakeBlock(...)`, or a statement parsed with
`Quote(...)`.

```c
[[cccc::comptime]]
Node *return_if_zero(Node *value) {
    return Quote("if ($1 == 0) return 0;", value);
}

int main(void) {
    int x = 0;
    return_if_zero(x);
    return 1;
}
```

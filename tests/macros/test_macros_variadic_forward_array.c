// Test ticket #285: inline variadic macros can forward their tail as an array.

int sum3(int a, int b, int c) {
    return a + b + c;
}

int zero(void) {
    return 42;
}

int add2(int base, int a, int b) {
    return base + a + b;
}

[[jcc::comptime(inline)]]
$node_t *forward_all($node_t *fn_node, ...) {
    return $funcall(fn_node, _AST_VARARGS_AS_ARRAY(), _AST_VARARG_COUNT());
}

[[jcc::comptime(inline)]]
$node_t *forward_tail($node_t *fn_node, $node_t *base, ...) {
    $node_t **tail = _AST_VARARGS_AS_ARRAY();
    $node_t *args[3] = { base, tail[0], tail[1] };
    return $funcall(fn_node, args, 3);
}

int main(void) {
    if (forward_all(sum3, 10, 20, 30) != 60)
        return 1;
    if (forward_all(zero) != 42)
        return 2;
    if (forward_tail(add2, 5, 6, 7) != 18)
        return 3;
    return 42;
}

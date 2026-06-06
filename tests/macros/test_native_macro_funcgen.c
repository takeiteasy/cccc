// JCC_FLAGS: --native
[[jcc::comptime(inline)]]
$node_t *gen_native_answer($node_t *unused) {
    (void)unused;
    $obj_t *fn = $function("native_answer", $get_type("int"));
    $with_fn(fn) {
        $function_set_body(fn, $quote("return 42;"));
    }
    return $int_literal(0);
}

int native_answer(void);

int main(void) {
    int dummy = gen_native_answer(0);
    (void)dummy;
    return native_answer();
}

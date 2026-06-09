// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: <cccc macro: generated expression>:1: generated expression

[[cccc::comptime(inline)]]
$node_t *synthetic_loc(void) {
    $node_t *node = $int_literal(0);
    $set_token(node, $synthetic_token("generated expression"));
    $macro_error_at(node, "synthetic generated location");
    return node;
}

int main(void) {
    return synthetic_loc();
}

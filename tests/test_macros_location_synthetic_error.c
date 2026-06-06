// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: <jcc macro: generated expression>:1: generated expression

[[jcc::macro(inline)]]
$node_t *synthetic_loc(void) {
    $node_t *node = $int_literal(0);
    $set_token(node, $synthetic_token("generated expression"));
    $macro_error_at(node, "synthetic generated location");
    return node;
}

int main(void) {
    return synthetic_loc();
}

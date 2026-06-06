// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: test_macros_location_default_error\.c:[0-9]+:.*default_loc

[[jcc::macro(inline)]]
$node_t *default_loc(void) {
    $node_t *node = $binary(nk_add, $int_literal(1), $int_literal(2));
    $macro_error_at(node, "default generated location");
    return node;
}

int main(void) {
    return default_loc();
}

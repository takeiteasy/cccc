// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: test_macros_location_copy_error\.c:13:.*copy_loc\(40\)

[[jcc::macro(inline)]]
$node_t *copy_loc($node_t *value) {
    $node_t *node = $binary(nk_add, value, $int_literal(1));
    $copy_location(node, value);
    $macro_error_at(node, "copied generated location");
    return node;
}

int main(void) {
    return copy_loc(40);
}

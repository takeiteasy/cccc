// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: <cccc macro: publish conflict>:1: publish conflict
// CCCC_EXPECT_STDERR: error: conflicting declaration for generated global variable 'conflict_name'

[[cccc::comptime(inline)]]
$node_t *publish_conflicting_global(void) {
    $obj_t *g = $global_var("conflict_name", $get_type("int"));
    $publish_at(g, $synthetic_token("publish conflict"));
    return $int_literal(0);
}

int main(void) {
    int conflict_name = 1;
    return publish_conflicting_global() + conflict_name;
}

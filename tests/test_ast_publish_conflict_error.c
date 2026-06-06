// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: <jcc macro: publish conflict>:1: publish conflict
// JCC_EXPECT_STDERR: error: conflicting declaration for generated global variable 'conflict_name'

[[jcc::macro(inline)]]
$node_t *publish_conflicting_global(void) {
    $obj_t *g = $global_var("conflict_name", $get_type("int"));
    $publish_at(g, $synthetic_token("publish conflict"));
    return $int_literal(0);
}

int main(void) {
    int conflict_name = 1;
    return publish_conflicting_global() + conflict_name;
}

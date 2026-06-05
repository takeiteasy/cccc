// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: <jcc macro: publish conflict>:1: publish conflict
// JCC_EXPECT_STDERR: error: conflicting declaration for generated global variable 'conflict_name'

[[jcc::macro(inline)]]
_Node *publish_conflicting_global(void) {
    _Obj *g = _AST_GLOBAL_VAR("conflict_name", _AST_GET_TYPE("int"));
    _AST_PUBLISH_AT(g, _AST_SYNTHETIC_TOKEN("publish conflict"));
    return _AST_INT_LITERAL(0);
}

int main(void) {
    int conflict_name = 1;
    return publish_conflicting_global() + conflict_name;
}

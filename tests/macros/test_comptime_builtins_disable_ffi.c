// CCCC_FLAGS: --disable-ffi
// Builtin names come back as writable VM copies, and builtins work with FFI off.


[[cccc::comptime]]
Node *name_len(void) {
    Type *t = GetType("int");
    char *n = (char *)__builtin_ast_type_name(t);
    n[0]    = 'S';
    return __builtin_ast_int_literal(n[0] == 'S' ? 42 : 0);
}

int main(void) {
    return name_len();
}

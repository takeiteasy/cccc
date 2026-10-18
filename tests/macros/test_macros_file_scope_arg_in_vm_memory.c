// A file-scope macro's string argument is readable by libc calls and loads.
[[cccc::comptime]]
void mk(char *name) {
    int   n      = (int)strlen(name);
    Type *int_ty = __builtin_ast_get_type("int");
    Obj  *fn     = __builtin_ast_function(name, int_ty);
    __builtin_ast_function_set_body(
        fn, __builtin_ast_return(
                __builtin_ast_int_literal(n + name[0] - 'g' + 36)));
}

mk("genned");

int main(void) {
    return genned();
}

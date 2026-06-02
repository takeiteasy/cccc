// Ticket #191: comptime variable initializer calls a comptime function.
// buf_size = page_count() * 4096 cannot be evaluated as a constant expression
// by the C parser; it must be run in the macro VM after page_count is compiled.

#pragma comptime
int page_count(void) { return 4; }

#pragma comptime
int buf_size = page_count() * 4096;

#pragma macro
_Node *get_buf_size(void) {
    return _AST_INT_LITERAL(_AST_GET_COMPTIME_INT("buf_size"));
}

int main(void) {
    if (get_buf_size() != 16384)
        return 1;
    return 42;
}

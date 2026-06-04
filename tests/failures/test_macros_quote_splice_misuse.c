// EXPECT_COMPILE_ERROR
// Test ticket #172: using $@N as an expression operand (outside a statement
// list) must produce a compile-time error.

[[jcc::macro(inline)]]
_Node *bad_splice(_Node *x) {
    _VirtualMachine *vm = __jcc_get_vm();
    // $@1 is a list splice — it cannot be used as an expression operand.
    return __jcc_quote(vm, "$@1 + 1", x);
}

int main(void) {
    int v = 0;
    int r = bad_splice(v);
    return r;
}

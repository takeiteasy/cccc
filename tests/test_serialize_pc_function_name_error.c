// CCCC_FLAGS: -m
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: __builtin_pc_function_name cannot be serialized to C
//
// #969: __builtin_pc_function_name(pc) maps a VM bytecode offset to the
// enclosing C function's name by calling into the __cccc_pc_to_name FFI
// shim (cc_load_symbolize_runtime, debugger.c). That shim, and the VM
// symbol table it reads, exist only inside the VM -- under -m/-c=native
// the composition has no meaning even in principle, since the argument is
// a real host return address, not a bytecode offset. Rejected with a
// diagnostic naming the builtin rather than emitting a call to the
// internal, undeclared __cccc_pc_to_name symbol.

int main(void) {
    void       *ra = __builtin_return_address(0);
    const char *fn = __builtin_pc_function_name(ra);
    return fn ? 1 : 0;
}

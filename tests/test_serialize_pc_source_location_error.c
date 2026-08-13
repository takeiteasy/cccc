// CCCC_FLAGS: -m
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: __builtin_pc_source_location cannot be serialized to C
//
// #969: __builtin_pc_source_location(pc, &file, &line) maps a VM bytecode
// offset to a source file/line by calling into the __cccc_pc_to_source FFI
// shim (cc_load_symbolize_runtime, debugger.c). That shim, and the VM
// source map it reads, exist only inside the VM -- under -m/-c=native the
// composition has no meaning even in principle, since the argument is a
// real host return address, not a bytecode offset. Rejected with a
// diagnostic naming the builtin rather than emitting a call to the
// internal, undeclared __cccc_pc_to_source symbol.

int main(void) {
    void *ra = __builtin_return_address(0);
    const char *file = 0;
    int line = 0;
    int ok = __builtin_pc_source_location(ra, &file, &line);
    return ok;
}

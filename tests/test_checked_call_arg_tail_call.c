// CCCC_FLAGS: --checked-pointers
// #488 blocking companion fix: rewrite_checked_call_args()'s `(__ca = arg,
// __ca)` desugar has the exact same shape as #486's existing bounds-cast
// desugar, and both defeated tail_arg_carries_frame_addr()'s frame-escape
// check (src/codegen_call.c) before its ND_COMMA/ND_ASSIGN arm was fixed
// to recurse into BOTH sides instead of only ->rhs -- following only the
// comma's rhs walks straight past `arg` (the actual frame-address-carrying
// expression, on the inner ND_ASSIGN's rhs) to a harmless plain ND_VAR
// read of `__ca`. A self-recursive tail call passing a frame-local array
// through a checked-pointer parameter must therefore still work
// correctly: if TCO wrongly fired with the caller's frame reused before
// `sink` reads `buf`, this would read garbage instead of the value just
// stored. Runs to completion and checks the actual returned value, not
// merely that it doesn't crash.

int sink(int *[[cccc::array, cccc::count(n)]] p, int n, int depth) {
    if (depth <= 0)
        return p[0] + n;
    int buf[1] = {100 + depth};
    return sink(buf, 1, depth - 1);
}

int main(void) {
    int r = sink((int[1]){0}, 0, 3);
    return r == 102 ? 42 : 1;
}

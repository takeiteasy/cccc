// CCCC_FLAGS: -1
// CCCC_REJECT_STDOUT: MEMORY LEAK DETECTED
// A __block variable's heap box (allocated in the function prologue, see
// codegen.c's is_block_var loop) is compiler-internal automatic storage,
// not a user allocation -- it must never appear in a leak report (#979),
// same reasoning as a VLA's alloca-backed block.
int main(void) {
    __block int x = 1;
    x = 2;
    return 40 + x;
}

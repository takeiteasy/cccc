// CCCC_FLAGS: --control-flow-integrity --optimize=1
// CCCC_MATRIX_SKIP: depends on --optimize=1 (tail-call elimination)
// Regression coverage for #756's fix: a plain CALL -> CALLT -> CALLT chain
// under --control-flow-integrity alone (no other safety flag) must still
// return correctly. The suite had no CFI + tail-call coverage at all before
// #756, which is why the shadow-stack desync in op_CALLT_fn went unnoticed.
static int c(int v) { return v + 1; } // reached via a tail call from b
static int b(int v) { return c(v); }  // reached via a tail call from a
static int a(int v) { return b(v); }  // a real CALL from main

int main(void) {
    int r = a(0);
    return r == 1 ? 42 : 1;
}

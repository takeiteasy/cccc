// EXPECT_COMPILE_ERROR
// A variably modified type (VLA, or a pointer/array chain that bottoms out
// at one) must not appear at file scope (C11 6.7.6.2p4/6.9.2p3): such an
// object would have no linkage-compatible size, since its size is a runtime
// value. CCCC previously accepted this silently and then crashed evaluating
// sizeof() on the resulting global (found while implementing #666's REPL
// aggregate-printing support).
int n = 3;
int v[n]; // error: variably modified 'v' at file scope
int main(void) {
    return 42;
}

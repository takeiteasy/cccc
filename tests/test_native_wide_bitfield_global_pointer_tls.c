// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -m
// CCCC_EXPECT_STDERR: needs a file-scope, non-thread-local global
//
// A constructor can only patch the main thread's copy of a thread-local.
int x;

union U {
    int *p;
    long l;
    _BitInt(256) f : 100;
};

_Thread_local union U g = {.p = &x};

int main(void) {
    return g.l != 0;
}

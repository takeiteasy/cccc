// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -x cl
// CCCC_EXPECT_STDERR: from the global to the local address space
kernel void k(global int *g) {
    local int *l = g;
    l[0]         = 1;
}
int main(void) {
    return 42;
}

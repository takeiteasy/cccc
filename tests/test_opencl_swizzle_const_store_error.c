// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -x cl
// CCCC_EXPECT_STDERR: swizzle of a const vector
kernel void k(global float *o) {
    const float8 v = (float8)(1);
    v.lo           = (float4)(2);
    o[0]           = v.s0;
}
int main(void) {
    return 42;
}

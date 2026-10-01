// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -x cl
// CCCC_EXPECT_STDERR: compound assignment to a multi-lane swizzle
kernel void k(global float *o) {
    float8 v  = (float8)(1);
    v.lo     += (float4)(1);
    o[0]      = v.s0;
}
int main(void) {
    return 42;
}

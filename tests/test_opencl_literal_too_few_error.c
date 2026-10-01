// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -x cl
// CCCC_EXPECT_STDERR: needs 4 components, not 3
kernel void k(global float *o) {
    float4 v = (float4)(1, 2, 3);
    o[0]     = v.x;
}
int main(void) {
    return 42;
}

// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -x cl
// CCCC_EXPECT_STDERR: too many components
kernel void k(global float *o) {
    float4 v = (float4)(1, 2, 3, 4, 5);
    o[0]     = v.x;
}
int main(void) {
    return 42;
}

// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -x cl
// CCCC_EXPECT_STDERR: names a lane twice
kernel void k(global float *o) {
    float4 v = (float4)(1);
    v.xxyz   = (float4)(1, 2, 3, 4);
    o[0]     = v.x;
}
int main(void) {
    return 42;
}

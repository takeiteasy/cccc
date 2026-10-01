// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -x cl
// CCCC_EXPECT_STDERR: names a lane the vector does not have
kernel void k(global float *o) {
    float4 v = (float4)(1);
    o[0]     = v.s9;
}
int main(void) {
    return 42;
}

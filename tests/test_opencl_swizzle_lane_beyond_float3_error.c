// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -x cl
// CCCC_EXPECT_STDERR: names a lane the vector does not have
kernel void k(global float *o) {
    float3 v = (float3)(1);
    o[0]     = v.w;
}
int main(void) {
    return 42;
}

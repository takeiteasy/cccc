// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -x cl
// CCCC_EXPECT_STDERR: is not a vector component
kernel void k(global float *o) {
    float4 v = (float4)(1);
    o[0]     = v.q;
}
int main(void) {
    return 42;
}

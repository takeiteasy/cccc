// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -x cl
// CCCC_EXPECT_STDERR: would be a 8-byte vector
kernel void k(global float *o) {
    float4 v = (float4)(1);
    float4 w = (float4)(v.xy, v.zw);
    o[0]     = w.x;
}
int main(void) {
    return 42;
}

// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -x cl
// CCCC_EXPECT_STDERR: incompatible types when initializing
kernel void k(global int4 *out) {
    float4 f = (float4)(1.0f);
    out[0]   = f;
}

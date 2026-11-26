// CCCC_FLAGS: -x cl
// CCCC_NATIVE_SKIP: -c=native does not write the implicit scalar splat
// OpenCL C converts a scalar to a vector at an initializer, argument and
// return.

float4 ret_scalar(void) {
    return 3.0f;
}

float lane(float4 v) {
    return v.y;
}

int main(void) {
    float4 v = 2.0f;
    float4 r = ret_scalar();
    return v.w == 2 && r.x == 3 && lane(5.0f) == 5 ? 42 : 1;
}

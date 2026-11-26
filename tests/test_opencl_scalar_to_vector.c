// CCCC_FLAGS: -x cl
// OpenCL C converts a scalar to a vector at an initializer, argument, return
// and assignment.

float4 ret_scalar(void) {
    return 3.0f;
}

float lane(float4 v) {
    return v.y;
}

int main(void) {
    float4 v = 2.0f;
    float4 r = ret_scalar();
    v        = 4.0f;
    int4 i   = 7;
    return v.w == 4 && r.x == 3 && lane(5.0f) == 5 && i.z == 7 ? 42 : 1;
}

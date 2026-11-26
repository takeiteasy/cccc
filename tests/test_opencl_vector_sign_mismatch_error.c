// CCCC_FLAGS: -x cl
// EXPECT_COMPILE_ERROR
// OpenCL C does not convert between int4 and uint4 implicitly.

int main(void) {
    int4  a = (int4)(1, 2, 3, 4);
    uint4 b = (uint4)(1, 2, 3, 4);
    int4  c = a + b;
    return c.x;
}

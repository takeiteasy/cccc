// CCCC_FLAGS: --std=c17 -m
// CCCC_EXPECT_STDOUT: int g\(double __cccc_knr_param_f\)
// CCCC_EXPECT_STDOUT: float f = __cccc_knr_param_f;
// CCCC_EXPECT_STDOUT: int k\(int __cccc_knr_param_c, int __cccc_knr_param_s\)
// CCCC_EXPECT_STDOUT: char c = __cccc_knr_param_c;
int g(f)
float f;
{
    return (int)f;
}
int k(c, s)
char  c;
short s;
{
    return c + s;
}
int main(void) {
    return g(40.0f) + k(1, 1) == 42 ? 42 : 1;
}

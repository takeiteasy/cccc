// CCCC_FLAGS: --no-layout-guards -m
// CCCC_EXPECT_STDOUT: int main
// CCCC_REJECT_STDOUT: _Static_assert
// --no-layout-guards drops the layout guards from -m output.

struct lg1172_plain {
    int  a;
    char b;
    long c;
};
struct lg1172_plain g_plain;

int main(void) {
    return 42;
}

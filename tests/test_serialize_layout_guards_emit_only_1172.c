// CCCC_FLAGS: --emit-only -m
// CCCC_EXPECT_STDOUT: int main
// CCCC_REJECT_STDOUT: _Static_assert
// --emit-only -m output carries no layout guards.

struct lg1172_plain {
    int  a;
    char b;
    long c;
};
struct lg1172_plain g_plain;

int main(void) {
    return 42;
}

// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -m
// CCCC_EXPECT_STDERR: a flexible array member
struct WithFlexible {
    _BitInt(256) f : 100;
    int tail[];
};

int main(void) {
    struct WithFlexible *p = 0;
    return (int)sizeof(*p);
}

// CCCC_FLAGS: -Wexcess-init
// CCCC_EXPECT_STDERR: ((.|\n)*excess elements in struct initializer){2}
// CCCC_EXPECT_STDERR: excess elements in struct initializer.*\[-Wexcess-init\]

struct S {
    int a;
    int b;
};

struct S gs = {1, 2, 3};

int main(void) {
    struct S s = {1, 2, 3};
    return s.a + gs.a + 40;
}

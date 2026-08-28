// CCCC_FLAGS: -Wexcess-init
// CCCC_EXPECT_STDERR: (?=[\s\S]*excess elements in struct initializer[\s\S]*excess elements in struct initializer)excess elements in struct initializer.*\[-Wexcess-init\]

struct S {
    int a;
    int b;
};

struct S gs = {1, 2, 3};

int main(void) {
    struct S s = {1, 2, 3};
    return s.a + gs.a + 40;
}

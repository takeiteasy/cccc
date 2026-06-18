// CCCC_FLAGS: -Wall
// CCCC_EXPECT_STDERR: initializer overrides prior initialization of 'x'.*\[-Woverride-init\]

struct S { int x; int y; };

int main(void) {
    struct S s = { .x = 1, .y = 5, .x = 42 };
    return s.x;
}

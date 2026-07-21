// CCCC_FLAGS: -Wdesignated-init
// CCCC_EXPECT_STDERR: positional initialization of field in struct declared with 'designated_init' attribute
struct foo { int a, b; } __attribute__((designated_init));
int main(void) {
    struct foo z = {.a = 1, 2};
    (void)z;
    return 42;
}

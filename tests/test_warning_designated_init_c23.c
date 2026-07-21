// CCCC_FLAGS: -Wdesignated-init
// CCCC_EXPECT_STDERR: positional initialization of field in struct declared with 'designated_init' attribute
struct [[gnu::designated_init]] foo { int a, b; };
int main(void) {
    struct foo w = {1, 2};
    (void)w;
    return 42;
}

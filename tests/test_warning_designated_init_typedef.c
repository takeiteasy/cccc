// CCCC_FLAGS: -Wdesignated-init
// CCCC_EXPECT_STDERR: positional initialization of field in struct declared with 'designated_init' attribute
typedef struct { int a, b; } __attribute__((designated_init)) Bar;
int main(void) {
    Bar t = {1, 2};
    (void)t;
    return 42;
}

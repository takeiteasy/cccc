// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: method 'make' is already defined for this type
typedef struct S {
    int x;
} S;

[[cccc::method(S, make)]]
int s_make(S *self) {
    return 1;
}

[[cccc::static_method(S, make)]]
int s_make_static(void) {
    return 2;
}

int main(void) {
    return 0;
}

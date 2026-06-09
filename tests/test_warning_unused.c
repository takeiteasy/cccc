// CCCC_FLAGS: -Wunused
// CCCC_EXPECT_STDERR: 5 warnings generated.

static int unused_global;

static int unused_function(void) {
    return 1;
}

int check(int unused_parameter) {
    int unused_local = 1;
unused_label:
    return 42;
}

int main(void) {
    return check(0);
}

// CCCC_FLAGS: -Wall
// CCCC_EXPECT_STDERR: 'bogus_fmt' is an unrecognized format function type

// An unknown archetype is a warning (as in gcc/clang), not an error.
__attribute__((format(bogus_fmt, 1, 2))) int my_log(const char *fmt, ...);

int main(void) {
    return 42;
}

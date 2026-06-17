// CCCC_FLAGS: -Wmultichar
// CCCC_EXPECT_STDERR: multi-character character constant.*\[-Wmultichar\]

int main(void) {
    int x = 'ab';
    (void)x;
    return 42;
}

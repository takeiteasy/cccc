// CCCC_FLAGS: -Wcast-qual
// CCCC_EXPECT_STDERR: cast discards.*qualifier.*\[-Wcast-qual\]

int main(void) {
    const char *cstr = "hello";
    char *mstr = (char *)cstr;
    (void)mstr;
    return 42;
}

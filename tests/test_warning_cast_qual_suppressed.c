// CCCC_FLAGS: -Wcast-qual -Wno-cast-qual
// CCCC_REJECT_STDERR: warning:

int main(void) {
    const char *cstr = "hello";
    char       *mstr = (char *)cstr;
    (void)mstr;
    return 42;
}

// CCCC_FLAGS: -Wint-conversion -Wno-int-conversion
// CCCC_REJECT_STDERR: warning:

int main(void) {
    const char *p = 'a';
    (void)p;
    return 42;
}

// CCCC_FLAGS: -Wunused-macros -Wno-unused-macros
// CCCC_REJECT_STDERR: warning:

#define UNUSED_FOO 42

int main(void) {
    return 42;
}

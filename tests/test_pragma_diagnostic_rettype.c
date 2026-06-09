// CCCC_FLAGS: -Wall
// CCCC_REJECT_STDERR: warning:

// Pragma inside function body should suppress return-type warning
int suppressed(void) {
#pragma GCC diagnostic ignored "-Wreturn-type"
}

int main(void) {
    suppressed();
    return 42;
}

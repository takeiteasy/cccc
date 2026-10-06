// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -3
// A runtime-indexed write past a local array (an access that skips the
// dangling check as frame-local) is still caught when the function returns:
// the overrun reaches the stack canary or the saved return address.
static int smash(int k) {
    char b[8];
    for (int i = 0; i < 8; i++)
        b[i] = (char)i;
    b[k]     = 1;
    b[k + 8] = 1;
    return b[0];
}

int main(int argc, char **argv) {
    (void)argv;
    return smash(32 + argc - 1) == 0 ? 42 : 1;
}

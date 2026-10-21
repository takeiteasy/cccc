// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: ^#define ascii_worm
// CCCC_REJECT_STDOUT: ^[^\n]*(define|undef)[^\n]*[^ -~\t\n]
// -m output replays no #define or #undef line containing non-ASCII characters.
#define 🪱 - ~
#define 🐍 ~-
#define ascii_worm -~
#ifndef ascii_worm
#error "ASCII-named define was lost"
#endif
int main(void) {
    if ((🪱 42) != 43)
        return 1;
    if ((🐍 42) != 41)
        return 2;
    if ((ascii_worm 40) != 41)
        return 3;
#undef 🪱
#undef 🐍
#ifdef 🪱
#error "emoji #undef did not take effect"
#endif
    if (-~41 != 42)
        return 4;
    return 42;
}

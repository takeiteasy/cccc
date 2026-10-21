// A macro with a non-ASCII name does not break the -c=native round trip.
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

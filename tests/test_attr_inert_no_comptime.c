// CCCC_FLAGS: -v
// CCCC_REJECT_STDOUT: Expanding macros in program
// Inert GNU attributes (and unknown vendor ones from SDK headers) do not
// load reflection.h's handlers, so a program without comptime code skips the
// comptime pass.

__attribute__((visibility("default"))) int vis(void) {
    return 10;
}

int avail(void) __attribute__((availability(macos, introduced = 10.9)));
int avail(void) {
    return 12;
}

__attribute__((__always_inline__)) static inline int inl(void) {
    return 10;
}

[[gnu::visibility("default")]] int cxx(void) {
    return 10;
}

int main(void) {
    return vis() + avail() + inl() + cxx();
}

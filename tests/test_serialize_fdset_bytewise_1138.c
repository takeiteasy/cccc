// #1138: FD_ZERO/FD_SET/FD_CLR/FD_ISSET (include/sys/select.h) used to
// spell fd_set's storage as `(set)->__fds_bits[...]` -- a member access,
// expanded by CCCC's own preprocessor at parse time, so the AST carries
// that exact member name regardless of which <sys/select.h> the host
// compiler later sees. Under -c=native the real host struct fd_set is
// what's in scope (macOS's `fds_bits`, glibc's `__fds_bits` but a
// `long[16]`, not our flat byte array), so the access failed to compile:
// "no member named '__fds_bits' in 'struct fd_set'". Fixed by indexing
// through a plain `(unsigned char *)(set)` reinterpretation of the whole
// object instead -- correct against both CCCC's own layout and the real
// host's, since sizeof(fd_set) == 128 and bit k always lands at byte
// k/8, bit k%8 on every little-endian arch CCCC supports. Unlike #1103's
// other host-layout gaps, this one is NOT fixable with a
// `#ifdef __CCCC__` / `#include_next` hand-off (see #1142) -- the macros
// are expanded before the backend ever runs, so which header the host
// later reads is irrelevant; the fix has to avoid naming a member at all.
// A plain functional test (no -m) rather than a serializer-shape check:
// this exercises FD_ZERO/FD_SET/FD_CLR/FD_ISSET on both the VM and, via
// the ordinary test corpus, -c=native -- round-tripping several fds,
// including one >= 64, to catch a word-width slip against the real host's
// own struct fd_set layout.
#include <sys/select.h>

int main(void) {
    fd_set set;
    FD_ZERO(&set);

    int fds[] = {0, 3, 7, 8, 63, 64, 127, 1023};
    for (unsigned i = 0; i < sizeof(fds) / sizeof(fds[0]); i++)
        FD_SET(fds[i], &set);

    for (unsigned i = 0; i < sizeof(fds) / sizeof(fds[0]); i++)
        if (!FD_ISSET(fds[i], &set))
            return 1;

    FD_CLR(3, &set);
    if (FD_ISSET(3, &set))
        return 2;
    if (!FD_ISSET(7, &set))
        return 3;

    return 42;
}

// #1145: -c=native never translated CCCC's own canonical
// _SC_*/_PC_*/_CS_* numbering (include/unistd.h) to the host's real
// sysconf()/pathconf()/fpathconf()/confstr() numbering the way the VM's
// own wrap_sysconf/wrap_pathconf/wrap_fpathconf/wrap_confstr
// (src/stdlib/posix_sched.c) do -- every guest use of one of these
// constants is already folded to a plain integer by the time an AST
// exists, so the emitted C passed the guest's canonical value straight to
// the host function with no translation at all (e.g. guest _SC_PAGESIZE,
// 11, reached the host literally as `sysconf(11)`, not macOS's 29 or
// glibc's 30). Same #1146 bug class (silently-wrong-value, not a compile
// error), for a family #1146 didn't cover.
//
// Fixed the same way #1146 fixed nl_langinfo/setlocale/sched_get_priority_*:
// renaming the guest program's own declared-only reference to
// __cccc_native_<name> and supplying a translating wrapper under that new
// name (serialize_canonical_const_shims, src/serialize_shims.c), ported
// from the VM-side wrap_* functions -- including their two non-obvious
// traps a naive port would get wrong: _SC_VERSION/_SC_2_VERSION/
// _SC_XOPEN_VERSION must NOT forward to the host (wrap_sysconf answers
// these from CCCC's own VM-model constants, 200809L/200809L/700, not
// whatever POSIX revision the host libc claims), and an unrecognized name
// must return -1 (sysconf/pathconf/fpathconf) or 0 (confstr), not a
// guest-value passthrough.
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    // _SC_PAGESIZE: the real discriminator -- canonical 11 vs. the host's
    // real numbering (29 macOS, 30 glibc) disagree enough that an
    // untranslated call either returns garbage or -1/EINVAL, never
    // matching getpagesize() by coincidence.
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0 || ps != getpagesize())
        return 1;

    if (sysconf(_SC_OPEN_MAX) <= 0)
        return 2;
    if (sysconf(_SC_NPROCESSORS_ONLN) < 1)
        return 3;

    // VM-model constants: must NOT reach the host's own _SC_VERSION (macOS
    // 200112L, glibc 200112L too) -- a naive verbatim port of the switch
    // labels (case _SC_VERSION: return sysconf(_SC_VERSION);) would forward
    // instead of special-casing, and silently regress this.
    if (sysconf(_SC_VERSION) != 200809L)
        return 4;
    if (sysconf(_SC_2_VERSION) != 200809L)
        return 5;
    if (sysconf(_SC_XOPEN_VERSION) != 700)
        return 6;

    // Unknown name -> -1, not a passthrough of the raw guest integer to
    // whatever the host happens to number that value.
    if (sysconf(99999) != -1)
        return 7;

    int fd = open("/", O_RDONLY);
    if (fd < 0)
        return 8;
    long pm  = pathconf("/", _PC_PATH_MAX);
    long fpm = fpathconf(fd, _PC_PATH_MAX);
    close(fd);
    if (pm <= 0 || fpm <= 0)
        return 9;
    if (pathconf("/", 99999) != -1)
        return 10;

    char   buf[256];
    size_t cs = confstr(_CS_PATH, buf, sizeof(buf));
    if (cs == 0 || cs > sizeof(buf))
        return 11;
    if (confstr(99999, buf, sizeof(buf)) != 0)
        return 12;

    return 42;
}

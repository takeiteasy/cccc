// CCCC_FLAGS: --posix-emulation
//
// #1146: -c=native never translated CCCC's own canonical POSIX constant
// numbering (include/poll.h, langinfo.h, locale.h, sched.h) to the host's
// real values the way the VM does (guest_to_host_pollev/host_to_guest_pollev
// in src/stdlib/posix_poll.c, guest_to_host_nl_item in posix_lang.c,
// guest_to_host_lc/guest_to_host_lc_mask in locale.c,
// guest_to_host_sched_policy in posix_sched.c) -- every guest use of one of
// these constants is already folded to a plain integer by the time an AST
// exists, so the emitted C passed the guest's canonical value straight to
// the host function with no translation at all. Unlike #1140's own gap
// (undeclared identifiers -- a hard compile error), this was a
// silently-wrong-VALUE bug: the call compiles, links, and runs, and just
// returns/behaves wrong on whichever host's real numbering *isn't* what
// CCCC's canonical numbering happens to copy (poll/sched: wrong on macOS;
// nl_langinfo/setlocale: wrong on Linux).
//
// Fixed by renaming the guest program's own declared-only reference to
// `__cccc_native_<name>` and supplying a translating wrapper under that new
// name (rename_bundled_extern_for_native_shim/serialize_canonical_const_shims,
// src/serialize.c) -- ppoll's own pre-existing #1140 shim is updated to
// translate too, so it stays consistent with plain poll() in the same
// binary (this is the ticket's own explicit requirement: "fixing this
// ticket should cover both call sites together").
//
// A companion residual closed here: the emitted gethostbyname_r/
// gethostbyaddr_r/getnetbyname_r shims' mutex only serialized against each
// other, not against a concurrent plain gethostbyname()/gethostbyaddr()/
// getnetbyname() call (which had no wrapper to add a mutex to) -- the VM's
// own nss_static_mutex covers both families. Not exercised by this
// single-threaded test; see the rename+mutex wrapper in
// serialize_posix_compat_shims (src/serialize.c) for the fix itself.
//
// Two things this test deliberately does NOT assert, and why:
//
// - setlocale()'s LC_* mis-mapping only manifests on Linux (macOS is the
//   platform CCCC's canonical numbering copies, so translation is a no-op
//   there either way), and no return-value-based assertion distinguishes
//   "translated" from "untranslated" using only the guaranteed-present "C"
//   locale -- every canonical LC_* value (0-6) happens to land on a
//   different-but-still-valid glibc category too, so an untranslated call
//   neither fails nor returns a visibly different string when every
//   category is already "C" by default. A real discriminator needs a
//   second installed locale, which isn't portable across CI images. This
//   test only proves setlocale/newlocale still round-trip through the
//   rename.
// - sched_get_priority_min/max's translation is unobservable via return
//   value on macOS specifically -- verified directly on real hardware
//   (Apple Silicon) that sched_get_priority_min/max return 15/47 for
//   SCHED_OTHER, SCHED_FIFO, and SCHED_RR alike, and even for the raw
//   integer 0 (not a valid host policy at all) -- the real host function
//   doesn't validate or vary its argument on this platform, so no
//   native-macOS assertion can tell a translated call from an untranslated
//   one. The `#ifdef __linux__` block below has a real discriminator
//   (glibc's documented, stable SCHED_OTHER range is exactly 0/0) that
//   only holds once translation is correct.
//
// Cross-checked by hand in the cccc-linux-amd64 container for the two
// Linux-only-buggy families (nl_langinfo, setlocale) rather than relying
// solely on this file, per this project's own established practice for
// glibc-only behavior (see feedback_verify_libc_signatures_linux).
//
// Skipped in tools/testing/__init__.py's NATIVE_SKIP_TESTS -- a
// pre-existing, unrelated header-resolution collision (same class as
// #1143, confirmed to already break test_posix_native_shims_1140.c on
// trunk with none of this ticket's changes applied) means the *test
// runner's* own -I./include flag makes this file fail to compile
// natively. Verified by hand without that flag instead (`./cccc -c=native
// -o out tests/test_posix_native_canonical_1146.c && ./out`, both before
// and after #1146's fix, matching the exit codes this file's assertions
// document above) -- see the skip table entry for the full explanation.
#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <poll.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    // poll(): POLLIN portable sanity round-trip on both platforms first --
    // canonical POLLIN shares the same bit value on both hosts, so this
    // alone can't discriminate translated from untranslated, but proves
    // poll()/ppoll() still work at all through the rename.
    int fds[2];
    if (pipe(fds) != 0)
        return 1;
    if (write(fds[1], "x", 1) != 1)
        return 2;
    struct pollfd pfd = {0};
    pfd.fd            = fds[0];
    pfd.events        = POLLIN;
    if (poll(&pfd, 1, 1000) != 1)
        return 3;
    if (!(pfd.revents & POLLIN))
        return 4;
    char c;
    if (read(fds[0], &c, 1) != 1)
        return 5;

    struct pollfd pfd2 = {0};
    pfd2.fd            = fds[0];
    pfd2.events        = POLLIN;
    struct timespec ts = {0, 50000000}; // 50ms, nothing to read
    if (ppoll(&pfd2, 1, &ts, NULL) != 0)
        return 6;

#ifdef __APPLE__
    // POLLWRBAND alone on a writable pipe fd -- the real discriminator,
    // macOS-only. Verified empirically on real Apple Silicon hardware that
    // this is reliable, NOT plain POLLWRNORM: canonical POLLWRNORM (0x0100)
    // happens to be exactly real host POLLWRBAND's own bit value, and
    // macOS's poll() turns out to echo an unrecognized-but-plausible
    // writable-class bit straight back for a pipe write end regardless of
    // real semantics -- so an *untranslated* POLLWRNORM request
    // coincidentally still sees its own bit echoed back, passing even when
    // translation is completely missing. Canonical POLLWRBAND (0x0200) has
    // no such coincidence: fed straight to the real host (untranslated) it
    // isn't a recognized event bit at all and the real poll() answers
    // POLLNVAL instead (confirmed directly: exit code 8, not 42, against
    // the pre-#1146 serializer) -- translated correctly, it maps to real
    // host POLLWRBAND (0x0100), which the pipe's write end does report.
    //
    // macOS-only because this bit family is a no-op passthrough on
    // Linux -- canonical numbering already copies glibc's values there, so
    // there's nothing to discriminate, AND (confirmed the hard way: this
    // exact assertion failed on real Linux/glibc, both amd64 and aarch64,
    // in CI) glibc's poll() does not report POLLWRBAND for a plain pipe's
    // write end the way macOS's poll() does -- a real semantic difference
    // between the two hosts' poll() implementations, not a translation bug.
    struct pollfd wpfd = {0};
    wpfd.fd            = fds[1]; // write end: always writable
    wpfd.events        = POLLWRBAND;
    if (poll(&wpfd, 1, 1000) != 1)
        return 7;
    if (!(wpfd.revents & POLLWRBAND))
        return 8;

    // ppoll(): same assertion, proving it agrees with plain poll() in the
    // same binary now that both translate.
    struct pollfd wpfd2 = {0};
    wpfd2.fd            = fds[1];
    wpfd2.events        = POLLWRBAND;
    struct timespec wts = {1, 0};
    if (ppoll(&wpfd2, 1, &wts, NULL) != 1)
        return 9;
    if (!(wpfd2.revents & POLLWRBAND))
        return 10;
#endif
    close(fds[0]);
    close(fds[1]);

    // nl_langinfo(): CODESET/RADIXCHAR/DAY_1 against the "C" locale must
    // return the right STRING, not merely a non-NULL one -- an
    // untranslated canonical DAY_1 (7) fed straight to glibc's real
    // nl_langinfo() lands on a structurally unrelated packed nl_item
    // (glibc packs (category<<16)|index; CCCC's canonical numbering copies
    // macOS's flat 0-56 sequence instead), so this is the real Linux-side
    // discriminator even though it also trivially passes on macOS (the
    // platform CCCC's canonical numbering already matches).
    setlocale(LC_ALL, "C");
    if (strcmp(nl_langinfo(CODESET), "") == 0)
        return 11;
    if (strcmp(nl_langinfo(RADIXCHAR), ".") != 0)
        return 12;
    if (strcmp(nl_langinfo(DAY_1), "Sunday") != 0)
        return 13;

    // #1148: an unrecognized nl_item (a hole in the 0-56 canonical
    // sequence, or anything > 56) must short-circuit to "" rather than
    // forwarding a bogus value to the host -- verified through the
    // -c=native shim (__cccc_native_guest_to_host_nl_item,
    // src/shims/canonical_const.c) here, and through the VM's own
    // guest_to_host_nl_item (src/stdlib/posix_lang.c) in
    // tests/suites/test_suite_posix.c.
    char *hole = nl_langinfo((nl_item)45);
    if (hole == NULL || hole[0] != '\0')
        return 23;
    char *oor = nl_langinfo((nl_item)99);
    if (oor == NULL || oor[0] != '\0')
        return 24;

    // nl_langinfo_l()/newlocale(): same assertion through the "_l" /
    // locale_t path and the LC_ALL_MASK special case.
    locale_t loc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    if (!loc)
        return 14;
    if (strcmp(nl_langinfo_l(DAY_1, loc), "Sunday") != 0)
        return 15;
    freelocale(loc);

    // setlocale(): round-trip only (see the file comment above for why a
    // stronger discriminator needs a second installed locale).
    if (!setlocale(LC_ALL, "C"))
        return 16;
    if (!setlocale(LC_TIME, "C"))
        return 17;

    // sched_get_priority_min/max(): round-trip on every canonical policy
    // (see the file comment above for why macOS can't discriminate a
    // mis-mapped policy via these return values); a real discriminator
    // where glibc's ranges are stable and documented.
    if (sched_get_priority_min(SCHED_OTHER) < 0)
        return 18;
    if (sched_get_priority_min(SCHED_FIFO) < 0)
        return 19;
    if (sched_get_priority_max(SCHED_RR) < 0)
        return 20;
#ifdef __linux__
    if (sched_get_priority_min(SCHED_OTHER) != 0 ||
        sched_get_priority_max(SCHED_OTHER) != 0)
        return 21;
    if (sched_get_priority_min(SCHED_FIFO) != 1)
        return 22;
#endif

    return 42;
}

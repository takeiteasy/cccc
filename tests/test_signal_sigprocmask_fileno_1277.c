// #1277: bundled <signal.h>/<stdio.h> gaps that stopped cccc's own frontend
// parsing src/host_signal.c during the self-hosting spike --
// siginfo_t.si_addr, sigprocmask (+ SIG_BLOCK/SIG_SETMASK), and fileno.
//
// Runs under the VM: sigprocmask is a translating wrapper over the host
// (the guest sigset_t is a 4-byte bitmask, not the host's real sigset_t),
// fileno is a plain passthrough, and si_addr must simply be a member that
// parses at its real offset.
#include <signal.h>
#include <stdio.h>
#include <stddef.h>

int main(void) {
    // fileno on the standard streams.
    if (fileno(stdin) != 0 || fileno(stdout) != 1 || fileno(stderr) != 2)
        return 1;

    // si_addr is reachable as a member, and offsetof agrees with the
    // header's own _Static_asserts.
#ifdef __APPLE__
    if (offsetof(siginfo_t, si_addr) != 24)
        return 2;
#else
    if (offsetof(siginfo_t, si_addr) != 16)
        return 2;
#endif
    siginfo_t si;
    si.si_addr = &si;
    if (si.si_addr != &si)
        return 3;

    // sigprocmask round-trip: block SIGUSR1, read the old mask back out,
    // confirm SIGUSR1 is now pending-blocked, then restore.
    sigset_t block, old, cur;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);

    if (sigprocmask(SIG_BLOCK, &block, &old) != 0)
        return 4;
    if (sigismember(&old, SIGUSR1) != 0) // was not blocked before
        return 5;

    if (sigprocmask(SIG_SETMASK, NULL, &cur) != 0)
        return 6;
    if (sigismember(&cur, SIGUSR1) != 1)           // is blocked now
        return 7;

    if (sigprocmask(SIG_SETMASK, &old, NULL) != 0) // restore
        return 8;

    return 42;
}

// Expected return: 42
// #1282: a batch of POSIX symbols CCCC's own source uses that resolved
// against the real system headers under a plain `make` build but were
// missing from the bundled include/ copy (self-hosting spike, #1132):
// clock_gettime/clock_getres/clock_settime (+ CLOCK_* ids, host-derived so
// they can't skew between Darwin/glibc the way errno codes once did, #779),
// kill/killpg/sigwait, pthread_sigmask/pthread_kill, SIZE_MAX/PTRDIFF_MIN/
// PTRDIFF_MAX and their stdint.h siblings, and optreset (BSD/Darwin only).
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) ||      \
    defined(__NetBSD__)
#include <getopt.h>
#endif

int main(void) {
    // stdint.h limit macros
    if (SIZE_MAX != (size_t)-1)
        return 1;
    if (PTRDIFF_MAX <= 0 || PTRDIFF_MIN >= 0)
        return 2;
    if (SIG_ATOMIC_MAX <= 0 || SIG_ATOMIC_MIN >= 0)
        return 3;
    if (WINT_MIN != 0 || WINT_MAX == 0)
        return 4;
    if (WCHAR_MAX == 0)
        return 5;

    // clock_gettime/clock_getres -- CLOCK_REALTIME/CLOCK_MONOTONIC are
    // host-derived (src/preprocess.c's init_time_macros), so this just
    // needs to actually resolve and return real, moving time.
    struct timespec t1, t2, res;
    if (clock_gettime(CLOCK_REALTIME, &t1) != 0)
        return 6;
    if (clock_getres(CLOCK_MONOTONIC, &res) != 0)
        return 7;
    if (clock_gettime(CLOCK_MONOTONIC, &t1) != 0)
        return 8;
    for (volatile int i = 0; i < 1000000; i++)
        ;
    if (clock_gettime(CLOCK_MONOTONIC, &t2) != 0)
        return 9;
    if (t2.tv_sec < t1.tv_sec ||
        (t2.tv_sec == t1.tv_sec && t2.tv_nsec < t1.tv_nsec))
        return 10;

    // kill/killpg -- SIGCONT to self and to our own process group is a
    // harmless, portable no-op signal to actually send.
    if (kill(getpid(), SIGCONT) != 0)
        return 11;
    if (killpg(getpgrp(), SIGCONT) != 0)
        return 12;

    // sigwait -- block SIGUSR1, raise it, wait it, confirm it's the one
    // delivered and no longer pending.
    sigset_t set, oldset;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    if (pthread_sigmask(SIG_BLOCK, &set, &oldset) != 0)
        return 13;
    if (raise(SIGUSR1) != 0)
        return 14;
    int caught = 0;
    if (sigwait(&set, &caught) != 0)
        return 15;
    if (caught != SIGUSR1)
        return 16;
    if (pthread_sigmask(SIG_SETMASK, &oldset, NULL) != 0)
        return 17;

    // pthread_kill -- deliver a signal to our own thread. Block it first
    // (same handling contract as sigwait above) so the process doesn't die.
    sigset_t set2;
    sigemptyset(&set2);
    sigaddset(&set2, SIGUSR2);
    if (pthread_sigmask(SIG_BLOCK, &set2, NULL) != 0)
        return 18;
    if (pthread_kill(pthread_self(), SIGUSR2) != 0)
        return 19;
    int caught2 = 0;
    if (sigwait(&set2, &caught2) != 0)
        return 20;
    if (caught2 != SIGUSR2)
        return 21;

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) ||      \
    defined(__NetBSD__)
    // optreset -- BSD/Darwin restart-a-scan-in-progress flag. Just confirm
    // it aliases real getopt() state: force it, then re-scan the same argv
    // from the top and confirm getopt() sees the option again.
    char *argv[] = {"prog", "-a", "-b", 0};
    optind       = 1;
    int a1       = getopt(2, argv, "ab");
    if (a1 != 'a')
        return 22;
    optreset = 1;
    optind   = 1;
    int a2   = getopt(2, argv, "ab");
    if (a2 != 'a')
        return 23;
#endif

    return 42;
}

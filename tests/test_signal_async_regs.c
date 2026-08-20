// Ticket V010 (#877): the dispatch loop's async pumps (the pending-signal
// poll and the SIGEV_THREAD poll, src/vm.c) can land between ANY two
// bytecode instructions, not just at a call boundary. Before the fix, they
// wrote straight into REG_A0 (an ordinary caller-saved general-purpose
// register, live across many instructions -- e.g. a value threaded through
// a loop body) with no save/restore, silently corrupting it.
//
// This is the breadth stress test for real signal delivery: setitimer()
// arms a periodic ITIMER_REAL, so the kernel genuinely delivers SIGALRM to
// this process again and again while the main thread runs a tight
// bytecode busy-spin with a live checksum threaded through the loop body
// -- no host calls in the loop, so each delivery can only interrupt
// between ordinary arithmetic instructions, landing at an essentially
// random point relative to the loop across many firings. The primary
// regression test for the ticket's own repro (a SIGEV_THREAD notification
// corrupting a live register in a busy-spin loop) lives in
// tests/suites/test_suite_posix.c's test_aio_sigev_thread/
// test_mqueue_sigev_thread.
#include <signal.h>
#include <sys/time.h>

static volatile sig_atomic_t handler_calls = 0;

static void alarm_handler(int sig) {
    (void)sig;
    handler_calls++;
}

int main(void) {
    signal(SIGALRM, alarm_handler);

    struct itimerval it;
    it.it_interval.tv_sec  = 0;
    it.it_interval.tv_usec = 200; // fire roughly every 200us
    it.it_value            = it.it_interval;
    if (setitimer(ITIMER_REAL, &it, 0) != 0)
        return 1;

    long long       checksum  = 0;
    long long       spins     = 0;
    const long long max_spins = 2000000;
    while (spins < max_spins) {
        checksum += (spins ^ (spins << 3)) - (checksum >> 1);
        spins++;
    }

    // Disarm before computing the reference checksum below -- a stray
    // SIGALRM landing in the (host-native) verification loop would just
    // increment handler_calls again, which is harmless, but there's no
    // reason to leave the timer running once the loop under test is done.
    struct itimerval off;
    off.it_interval.tv_sec = off.it_interval.tv_usec = 0;
    off.it_value                                     = off.it_interval;
    setitimer(ITIMER_REAL, &off, 0);

    long long expected = 0;
    for (long long s = 0; s < max_spins; s++)
        expected += (s ^ (s << 3)) - (expected >> 1);

    if (handler_calls <= 0)
        return 2; // SIGALRM never observed -- test didn't exercise anything
    if (checksum != expected)
        return 3; // async delivery corrupted a live register (#877)

    return 42;
}

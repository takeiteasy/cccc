#include <signal.h>

/* Shared flag for handler tests */
static volatile int g_handler_called = 0;
static volatile int g_handler_sig    = 0;

static void on_usr1(int sig) {
    g_handler_called = 1;
    g_handler_sig    = sig;
}

int main(void) {
    /* Test 1: SIG_IGN — raise SIGINT, process must survive */
    if (signal(SIGINT, SIG_IGN) == SIG_ERR) return 1;
    if (raise(SIGINT) != 0) return 2;
    /* If we reach here, SIGINT was ignored */

    /* Test 2: Restore SIG_DFL for SIGINT (we can't raise it now without dying,
       but we verify signal() round-trip returns the old handler) */
    void (*old)(int) = signal(SIGINT, SIG_DFL);
    if (old == SIG_ERR) return 3;
    /* old should be SIG_IGN (1) that we installed in Test 1 */
    if (old != SIG_IGN) return 4;

    /* Test 3: VM handler delivery via raise() */
    g_handler_called = 0;
    g_handler_sig    = 0;
    if (signal(SIGUSR1, on_usr1) == SIG_ERR) return 5;
    if (raise(SIGUSR1) != 0) return 6;
    if (!g_handler_called) return 7;
    if (g_handler_sig != SIGUSR1) return 8;

    /* Test 4: Re-install SIG_IGN after VM handler; handler must not fire again */
    g_handler_called = 0;
    if (signal(SIGUSR1, SIG_IGN) == SIG_ERR) return 9;
    if (raise(SIGUSR1) != 0) return 10;
    if (g_handler_called) return 11; /* handler must NOT have been called */

    /* Test 5: SIGTERM SIG_IGN round-trip (from test_missing_headers pattern) */
    if (signal(SIGTERM, SIG_IGN) == SIG_ERR) return 12;
    if (signal(SIGTERM, SIG_DFL) == SIG_ERR) return 13;

    return 42;
}

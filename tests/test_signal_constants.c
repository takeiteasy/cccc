// Expected return: 42
// #409: previously-missing SIG* constants (job control, resource limits,
// I/O readiness, misc). Values are platform-specific -- verified distinct
// and in the valid [1, CCCC_NSIG) range rather than against fixed numbers.
#include <signal.h>

int main(void) {
    int sigs[] = {
        SIGSTOP, SIGCONT, SIGTSTP, SIGTTIN, SIGTTOU, SIGPROF, SIGSYS,
        SIGXCPU, SIGXFSZ, SIGVTALRM, SIGIO, SIGURG, SIGWINCH,
        SIGUSR1, SIGUSR2, SIGBUS, SIGSEGV, SIGCHLD,
    };
    int n = sizeof(sigs) / sizeof(sigs[0]);

    for (int i = 0; i < n; i++) {
        if (sigs[i] <= 0 || sigs[i] >= 32)
            return 1;
        for (int j = i + 1; j < n; j++) {
            if (sigs[i] == sigs[j])
                return 2;
        }
    }

    return 42;
}

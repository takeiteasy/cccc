// tools/lib/proc: exit codes, capture, signals, timeouts, large output.
#include "../../tools/lib/proc.c"
#include "../../tools/lib/str.c"

#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #c);                                                       \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(void) {
    ProcResult r;

    char      *exit7[] = {"sh", "-c", "exit 7", NULL};
    CHECK(proc_run(exit7, 5000, &r) == 0);
    CHECK(r.exit_code == 7 && !r.signaled && !r.timed_out);
    proc_result_free(&r);

    char *both[] = {"sh", "-c", "echo out; echo err >&2", NULL};
    CHECK(proc_run(both, 5000, &r) == 0);
    CHECK(r.exit_code == 0);
    CHECK(strcmp(r.out.data, "out\n") == 0);
    CHECK(strcmp(r.err.data, "err\n") == 0);
    proc_result_free(&r);

    char *sig[] = {"sh", "-c", "kill -TERM $$", NULL};
    CHECK(proc_run(sig, 5000, &r) == 0);
    CHECK(r.signaled && r.signo == SIGTERM && !r.timed_out);
    proc_result_free(&r);

    // stdin is /dev/null, so cat returns immediately
    char *cat[] = {"cat", NULL};
    CHECK(proc_run(cat, 5000, &r) == 0);
    CHECK(r.exit_code == 0 && r.out.len == 0);
    proc_result_free(&r);

    char *sleeper[] = {"sh", "-c", "sleep 5", NULL};
    long  t0        = now_ms();
    CHECK(proc_run(sleeper, 200, &r) == 0);
    CHECK(r.timed_out);
    CHECK(now_ms() - t0 < 3000);
    proc_result_free(&r);

    // a grandchild holding the pipes open must not wedge the timeout path
    char *grand[] = {"sh", "-c", "sleep 5 & wait", NULL};
    t0            = now_ms();
    CHECK(proc_run(grand, 200, &r) == 0);
    CHECK(r.timed_out && now_ms() - t0 < 3000);
    proc_result_free(&r);

    // >64 KiB on both pipes at once: would deadlock a read-one-then-the-other
    // loop
    char *big[] = {"sh", "-c",
                   "i=0; while [ $i -lt 3000 ]; do "
                   "echo 0123456789012345678901234567890123456789; "
                   "echo 0123456789012345678901234567890123456789 >&2; "
                   "i=$((i+1)); done",
                   NULL};
    CHECK(proc_run(big, 20000, &r) == 0);
    CHECK(r.exit_code == 0);
    CHECK(r.out.len == 3000 * 41 && r.err.len == 3000 * 41);
    proc_result_free(&r);

    char *missing[] = {"cccc-no-such-binary-xyz", NULL};
    CHECK(proc_run(missing, 1000, &r) != 0);
    return 42;
}

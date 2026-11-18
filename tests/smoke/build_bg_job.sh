# A `cmd &` job in a RunCustom step stays in the step's process group: a
# StepTimeout or an interrupt kills it, and a step that finishes first waits
# for it.
. "$(dirname "$0")/lib.sh"

JOB_PID=
cleanup() {
    [ -n "$JOB_PID" ] && kill "$JOB_PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT

# await COND: runs COND every 0.1s for up to 30s.
await() {
    i=0
    while [ "$i" -lt 300 ]; do
        eval "$1" && return 0
        i=$((i + 1))
        sleep 0.1
    done
    return 1
}

# The job closes its own output so the step's pipe drain cannot stand in for a
# wait, and records its pid because the shell may exec the last command.
JOB="sh -c 'exec >/dev/null 2>\&1; echo \$\$ > $TMP/job.pid; exec sleep 600' \&"

cat >timeout.c <<EOS
[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = RunCustom(ctx, "bg", "$JOB");
    StepTimeout(t, 2);
    return BuildDefault(ctx);
}
EOS
cat >plain.c <<EOS
[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "bg", "$JOB");
    return BuildDefault(ctx);
}
EOS
cat >wait.c <<EOS
[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "bg", "sh -c 'exec >/dev/null 2>\&1; sleep 1; touch $TMP/waited' \&");
    return BuildDefault(ctx);
}
EOS

# Timeout: the step fails and the job is gone.
expect_fail "$CCCC" --build timeout.c
grep -q "timed out" err.txt out.txt || fail "the step did not time out"
JOB_PID=$(cat job.pid 2>/dev/null)
[ -n "$JOB_PID" ] || fail "the job never started"
await '! kill -0 "$JOB_PID" 2>/dev/null' ||
    fail "the job survived the step's timeout"
JOB_PID=
rm -f job.pid

# Interrupt: a signal to the driver's group reaches the job. A background job
# ignores SIGINT, so the driver gets a process group of its own.
cat >launch.c <<'EOS'
#include <signal.h>
#include <unistd.h>
int main(int argc, char **argv) {
    (void)argc;
    setpgid(0, 0);
    signal(SIGINT, SIG_DFL);
    execvp(argv[1], argv + 1);
    return 127;
}
EOS
"$(real_cc)" -w -o launch launch.c || fail "cannot build the launcher"

./launch "$CCCC" --build plain.c </dev/null >drv.txt 2>&1 &
driver=$!
await '[ -s "$TMP/job.pid" ]' || {
    cat drv.txt >&2
    fail "the job never started"
}
JOB_PID=$(cat job.pid)
kill -INT "-$driver" 2>/dev/null || fail "cannot signal the driver's group"
await '! kill -0 "$JOB_PID" 2>/dev/null' ||
    fail "the job survived the interrupt"
JOB_PID=
wait "$driver" 2>/dev/null

# Wait: the step does not return before the job has finished.
expect_exit 0 "$CCCC" --build wait.c
[ -f waited ] || fail "the step returned before its job finished"
exit 0

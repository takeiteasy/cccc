# Interrupting a --build run tears down the step's command with it: the step
# runs in its own process group, so the driver forwards the signal there.
. "$(dirname "$0")/lib.sh"

STEP_PID=
cleanup() {
    [ -n "$STEP_PID" ] && kill "$STEP_PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT

# The step records the command's own pid: a shell is free to exec the last
# command of a -c string, so the command cannot be found by its arguments.
cat >build.c <<EOS
[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "wedged", "sh -c 'echo \$\$ > $TMP/step.pid; exec sleep 600'");
    return BuildDefault(ctx);
}
EOS

# A signal sent to one process cannot stand in for a terminal interrupt, and a
# background job ignores SIGINT, so the driver gets a process group of its own
# and the group is signalled.
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
cc=$(real_cc)
"$cc" -w -o launch launch.c || fail "cannot build the launcher"

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

./launch "$CCCC" --build build.c </dev/null >drv.txt 2>&1 &
driver=$!

await '[ -s "$TMP/step.pid" ]' || {
    cat drv.txt >&2
    fail "the step's command never started"
}
STEP_PID=$(cat "$TMP/step.pid")
kill -0 "$STEP_PID" 2>/dev/null || fail "the step's command exited on its own"

kill -INT "-$driver" 2>/dev/null || fail "cannot signal the driver's group"
await '! kill -0 "$STEP_PID" 2>/dev/null' ||
    fail "the step's command survived the interrupt"

wait "$driver" 2>/dev/null
STEP_PID=
exit 0

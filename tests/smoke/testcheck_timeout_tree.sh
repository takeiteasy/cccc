# testcheck's timeout kills the whole process tree under the test, including a
# descendant that made a process group of its own (every --build step does).
SRC=$(cd "$(dirname "$0")/../.." && pwd)
. "$(dirname "$0")/lib.sh"

PID=
cleanup() {
    [ -n "$PID" ] && kill "$PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT

cc=$(real_cc)
"$cc" -w -o testcheck "$SRC/tools/testcheck.c" || fail "cannot build testcheck"

cat >launch.c <<'EOS'
#include <unistd.h>
int main(int argc, char **argv) {
    (void)argc;
    setpgid(0, 0);
    execvp(argv[1], argv + 1);
    return 127;
}
EOS
"$cc" -w -o launch launch.c || fail "cannot build the launcher"

# A stand-in for cccc whose "test" outlives the timeout in a group of its own.
cat >fakecccc <<EOS
#!/bin/sh
"$TMP/launch" sh -c 'echo \$\$ > "$TMP/desc.pid"; exec sleep 600'
EOS
chmod +x fakecccc
echo 'int main(void) { return 42; }' >t.c

expect_fail ./testcheck --timeout 2 "$TMP/fakecccc" t.c
grep -q -i "timeout\|timed out" out.txt err.txt || fail "no timeout was reported"
PID=$(cat desc.pid 2>/dev/null)
[ -n "$PID" ] || fail "the test never started"

i=0
while kill -0 "$PID" 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -lt 50 ] || fail "a descendant in its own group survived the timeout"
    sleep 0.1
done
PID=
exit 0

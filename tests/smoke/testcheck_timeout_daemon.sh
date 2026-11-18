# testcheck's timeout also kills a descendant that daemonised (reparented to
# init) before the timeout. Linux only: testcheck is a subreaper there.
SRC=$(cd "$(dirname "$0")/../.." && pwd)
. "$(dirname "$0")/lib.sh"

[ "$(uname -s)" = Linux ] && command -v setsid >/dev/null || { echo "skip: needs Linux with setsid"; exit 0; }

PID=
cleanup() {
    [ -n "$PID" ] && kill "$PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT

cc=$(real_cc)
"$cc" -w -o testcheck "$SRC/tools/testcheck.c" || fail "cannot build testcheck"

# The daemon has its own session (outside the test's group) and keeps the test's stdout open, so the run lasts until the timeout.
cat >fakecccc <<EOS
#!/bin/sh
( setsid sh -c 'echo \$\$ > "$TMP/desc.pid"; exec sleep 600' & )
sleep 600
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
    [ "$i" -lt 50 ] || fail "a daemonised descendant survived the timeout"
    sleep 0.1
done
PID=
exit 0

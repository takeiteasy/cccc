# A signal sent to the build driver alone (not its process group) reaches the
# children it forked: the per-target children under --build-jobs, the compiler
# children of the compile pool and the serial path's compiler.
. "$(dirname "$0")/lib.sh"

PIDS=
cleanup() {
    for p in $PIDS; do kill "$p" 2>/dev/null; done
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

# gone FILE...: every pid recorded in the files is dead.
gone() {
    for p in $(cat "$@"); do
        kill -0 "$p" 2>/dev/null && return 1
    done
    return 0
}

# Two independent steps, so each runs in a target child of its own.
cat >targets.c <<EOS
[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "a", "sh -c 'echo \$\$ > $TMP/a.pid; exec sleep 600'");
    RunCustom(ctx, "b", "sh -c 'echo \$\$ > $TMP/b.pid; exec sleep 600'");
    return BuildDefault(ctx);
}
EOS

"$CCCC" --build targets.c --build-jobs=2 </dev/null >drv.txt 2>&1 &
driver=$!
PIDS=$driver
await '[ -s "$TMP/a.pid" ] && [ -s "$TMP/b.pid" ]' || {
    cat drv.txt >&2
    fail "the steps never started"
}
PIDS="$driver $(cat a.pid) $(cat b.pid)"

kill -TERM "$driver"
await 'gone a.pid b.pid' || fail "a target's step survived a signal to the driver"
wait "$driver" 2>/dev/null

# The compile pool: the "compiler" records its pid and never finishes.
cat >slowcc <<EOS
#!/bin/sh
echo \$\$ >> $TMP/cc.pids
exec sleep 600
EOS
chmod +x slowcc
echo 'int a(void) { return 1; }' >a.c
echo 'int b(void) { return 2; }' >b.c
cat >compile.c <<EOS
[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = StaticLib(ctx, "lib");
    AddSource(t, "a.c");
    AddSource(t, "b.c");
    return BuildDefault(ctx);
}
EOS

CCCC_BUILD_CC="$TMP/slowcc" "$CCCC" --build compile.c --build-jobs=2 \
    </dev/null >drv2.txt 2>&1 &
driver=$!
PIDS=$driver
await '[ -s "$TMP/cc.pids" ] && [ "$(wc -l < "$TMP/cc.pids")" -ge 2 ]' || {
    cat drv2.txt >&2
    fail "the compiles never started"
}
PIDS="$driver $(cat cc.pids)"

kill -TERM "$driver"
await 'gone cc.pids' ||
    fail "a compile survived a signal to the driver"
wait "$driver" 2>/dev/null

# The serial path: one compiler at a time.
rm -f cc.pids
CCCC_BUILD_CC="$TMP/slowcc" "$CCCC" --build compile.c --build-jobs=1 \
    </dev/null >drv3.txt 2>&1 &
driver=$!
PIDS=$driver
await '[ -s "$TMP/cc.pids" ]' || {
    cat drv3.txt >&2
    fail "the serial compile never started"
}
PIDS="$driver $(cat cc.pids)"

kill -TERM "$driver"
await 'gone cc.pids' ||
    fail "the serial compile survived a signal to the driver"
wait "$driver" 2>/dev/null
exit 0

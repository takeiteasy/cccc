# A StepTimeout kills what the step started, including a nested build's own
# steps, each of which runs in a process group of its own.
. "$(dirname "$0")/lib.sh"

DESC=
cleanup() {
    [ -n "$DESC" ] && kill "$DESC" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT

cat >inner.c <<EOS
[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "inner", "sh -c 'echo \$\$ > $TMP/desc.pid; exec sleep 600'");
    return BuildDefault(ctx);
}
EOS
cat >outer.c <<EOS
[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = RunCustom(ctx, "outer", "$CCCC --build inner.c");
    StepTimeout(t, 3);
    return BuildDefault(ctx);
}
EOS

expect_fail "$CCCC" --build outer.c
grep -q "timed out" err.txt out.txt || fail "the step did not time out"
DESC=$(cat desc.pid 2>/dev/null)
[ -n "$DESC" ] || fail "the nested step never started"

i=0
while kill -0 "$DESC" 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -lt 50 ] || fail "the nested build's step survived the timeout"
    sleep 0.1
done
DESC=
exit 0

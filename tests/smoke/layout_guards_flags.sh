# --no-layout-guards output is the default -m output with only the guard lines
# removed, and --emit-only output carries no guards.
. "$(dirname "$0")/lib.sh"

cat >prog.c <<'EOS'
struct lg1172_plain {
    int a;
    char b;
    long c;
};
struct lg1172_plain g_plain;

int main(void) { return 42; }
EOS

"$CCCC" -m prog.c >plain.txt 2>/dev/null || fail "-m failed"
"$CCCC" --no-layout-guards -m prog.c >noguards.txt 2>/dev/null || fail "--no-layout-guards -m failed"
grep -q '_Static_assert' plain.txt || fail "the default -m output has no layout guard"
grep -q '_Static_assert' noguards.txt && fail "--no-layout-guards output still has a guard"
grep -v 'layout disagreement' plain.txt >expected.txt
cmp -s expected.txt noguards.txt || {
    diff expected.txt noguards.txt >&2
    fail "--no-layout-guards differs from -m with only the guard lines removed"
}
"$CCCC" --emit-only -m prog.c >strict.txt 2>/dev/null || fail "--emit-only -m failed"
grep -q '_Static_assert' strict.txt && fail "--emit-only output has a layout guard"
exit 0

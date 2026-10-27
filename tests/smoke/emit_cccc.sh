# --emit-cccc: -c=native needs an explicit CCCC_NATIVE_CC, and -m output
# recompiles under cccc itself.
. "$(dirname "$0")/lib.sh"

cat >use.c <<'EOS'
void use(int * [[cccc::single]] p) { (void)p; }
int main(void) { int x = 5; use(&x); return 42; }
EOS

unset CCCC_NATIVE_CC
expect_fail "$CCCC" -c=native --emit-cccc -o out_no_cc use.c
grep -q CCCC_NATIVE_CC err.txt || { cat err.txt >&2; fail "expected a CCCC_NATIVE_CC diagnostic"; }

CCCC_NATIVE_CC=cc expect_exit 0 "$CCCC" -c=native --emit-cccc -o out_cc use.c
expect_exit 42 ./out_cc

expect_exit 0 "$CCCC" -m --emit-cccc use.c
grep -q '\[\[cccc::single\]\]' out.txt || fail "-m --emit-cccc dropped the qualifier"
cp out.txt roundtrip.c
expect_exit 42 "$CCCC" roundtrip.c

# Checked array declarators survive the round trip, and still trap.
cat >arrays.c <<'EOS'
#include <stdlib.h>
struct S { int n; int d _Checked[] [[cccc::count(n)]]; };
int g _Checked[4];
int m _Checked[3][5];
char s _Nt_checked[6] = "hello";
int f(int n, int i) {
    int v _Checked[n];
    v[0] = 1;
    struct S *p = malloc(sizeof(struct S) + 8);
    p->n = 2;
    p->d[0] = 5;
    int r = v[0] + m[1][2] + s[0] + p->d[0];
    r += g[i];
    free(p);
    return r;
}
int main(int argc, char **argv) {
    (void)argv;
    return f(2, argc > 3 ? 4 : 0) == 1 + 'h' + 5 ? 42 : 1;
}
EOS
expect_exit 0 "$CCCC" -m --emit-cccc --checked-pointers arrays.c
for want in 'd _Checked\[\] \[\[cccc::count(n)\]\]' 'g _Checked\[4\]' 'm _Checked\[3\]\[5\]' 's _Nt_checked\[6\]' 'v _Checked\['; do
    grep -q "$want" out.txt || fail "-m --emit-cccc dropped '$want'"
done
cp out.txt arrays_roundtrip.c
expect_exit 42 "$CCCC" --checked-pointers arrays_roundtrip.c
expect_fail "$CCCC" --checked-pointers arrays_roundtrip.c -- a b c d

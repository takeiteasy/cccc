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

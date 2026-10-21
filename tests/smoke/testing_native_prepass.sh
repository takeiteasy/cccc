# --testing -c=native runs the suite first: a passing suite builds, a failing
# one refuses (with or without --fail-fast), and --testing --build is blocked
# the same way.
. "$(dirname "$0")/lib.sh"

cat >pass.c <<'EOS'
[[cccc::test(return = 42)]] int t_pass(void) { return 42; }
int main(void) { return 42; }
EOS
cat >fail.c <<'EOS'
[[cccc::test(return = 999)]] int t_fail(void) { return 1; }
int main(void) { return 42; }
EOS

expect_exit 0 "$CCCC" --testing -c=native -o pass_out pass.c
[ -f pass_out ] || fail "executable was not written despite the suite passing"
expect_exit 42 ./pass_out

rm -f fail_out
expect_fail "$CCCC" --testing -c=native -o fail_out fail.c
[ ! -e fail_out ] || fail "artifact was written despite failing tests"
expect_fail "$CCCC" --testing --fail-fast -c=native -o fail_out fail.c
[ ! -e fail_out ] || fail "artifact was written despite failing tests (--fail-fast)"

cat >build_fail.c <<'EOS'
[[cccc::test(return = 999)]] int t_fail(void) { return 1; }
[[cccc::build]]
int build_main(void) { return 0; }
EOS
cat >build_pass.c <<'EOS'
[[cccc::test(return = 42)]] int t_pass(void) { return 42; }
[[cccc::build]]
int build_main(void) { return 0; }
EOS
expect_fail "$CCCC" --testing --build build_fail.c
expect_exit 0 "$CCCC" --testing --build build_pass.c

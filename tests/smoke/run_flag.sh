# --run runs main() in the VM after --testing or --build succeeds, and not
# after one fails.
. "$(dirname "$0")/lib.sh"

cat >pass.c <<'EOS'
[[cccc::test(return = 42)]] int t_pass(void) { return 42; }
int main(void) { return 42; }
EOS
cat >fail.c <<'EOS'
[[cccc::test(return = 999)]] int t_fail(void) { return 1; }
int main(void) { return 42; }
EOS
echo 'int main(void) { return 42; }' >plain.c

expect_exit 42 "$CCCC" --run plain.c
expect_exit 0 "$CCCC" --testing pass.c
expect_exit 42 "$CCCC" --testing --run pass.c
expect_exit 1 "$CCCC" --testing --run fail.c
expect_exit 0 "$CCCC" --testing --run --list-tests pass.c

cat >build.c <<'EOS'
[[cccc::build]]
int build_main(Builder *ctx) {
    return 0;
}
int main(void) { return 42; }
EOS
cat >build_fail.c <<'EOS'
#include <stdio.h>
[[cccc::build]]
int build_main(Builder *ctx) {
    return 3;
}
int main(void) { puts("ran"); return 42; }
EOS
expect_exit 0 "$CCCC" --build build.c
expect_exit 42 "$CCCC" --build --run build.c
expect_fail "$CCCC" --build --run build_fail.c
! grep -q ran out.txt || fail "main ran after a failed build"

for opt in -c=native -c=generated --testing=native --repl --test-run -d -E -m; do
    expect_fail "$CCCC" --run "$opt" plain.c
done
expect_fail "$CCCC" --build --run --watch build.c

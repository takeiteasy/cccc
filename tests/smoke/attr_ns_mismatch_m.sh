# -m output leaves out an attribute written in a namespace it lacks, so the
# host compiler lays the struct out as the VM does.
. "$(dirname "$0")/lib.sh"

cat >in.c <<'EOS'
#include <stddef.h>
struct [[cccc::packed]] S { char c; int x; };
struct [[gnu::packed]] T { char c; int x; };
struct [[acme::packed]] U { char c; int x; };
struct S s;
struct T t;
struct U u;
[[acme::cold]] int f(void) { return offsetof(struct S, x) + offsetof(struct T, x); }
int main(void) { return sizeof s == 8 && sizeof t == 5 && sizeof u == 8 && f() == 5 ? 42 : 1; }
EOS

expect_exit 42 "$CCCC" in.c
grep -q "'packed' is not a 'cccc' attribute" err.txt || fail "no mismatch warning"
"$CCCC" -m in.c >out.c 2>/dev/null || fail "-m failed"
[ "$(grep -c packed out.c)" -eq 1 ] || fail "-m output: $(grep packed out.c)"
grep -q 'cold\|acme' out.c && fail "-m kept a mismatched attribute"
expect_exit 0 "$(real_cc)" -w -o prog out.c
expect_exit 42 ./prog

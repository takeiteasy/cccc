# -E -o takes one input; several inputs without -o print every unit.
. "$(dirname "$0")/lib.sh"

echo 'int a(void) { return 1; }' >a.c
echo 'int b(void) { return 2; }' >b.c

expect_exit 0 "$CCCC" -E -o one.i a.c
grep -q 'int a' one.i || fail "-E -o a.c wrote no output"

expect_fail "$CCCC" -E -o both.i a.c b.c
grep -q 'multiple input files' err.txt || fail "no diagnostic for -E -o with two inputs"
[ ! -e both.i ] || fail "-E -o with two inputs still wrote a file"

expect_exit 0 "$CCCC" -E a.c b.c
grep -q 'int a' out.txt && grep -q 'int b' out.txt || fail "-E a.c b.c lost a unit"

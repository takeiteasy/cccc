# Bare -c (no format, no -o) builds natively and writes ./a.out.
. "$(dirname "$0")/lib.sh"

echo 'int main(void) { return 42; }' >prog.c
rm -f a.out
expect_exit 0 "$CCCC" -c prog.c
[ -f a.out ] || fail "./a.out was not written"
expect_exit 42 ./a.out

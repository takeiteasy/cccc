# -E prints templates in their source spelling, on one line, so the output
# compiles again.
. "$(dirname "$0")/lib.sh"

cat >t.c <<'SRC'
#define BIG(a, b) @inst(int) max(a, b)
@template(T) T max(T a, T b) { return a > b ? a : b; }
@specialize(char) char max(char a, char b) { return a; }
@template(U = int) struct Box { U v; };
@template(Ts...) struct Tuple { Ts... v; };
int main(void) {
    @inst Box a = {1};
    Box@(Box@(int)) b = {{1}};
    Tuple@(long, long) t = {1, 2};
    return BIG(40, 42) - 2 + (t.v + ...) - 3 + a.v + b.v.v;
}
SRC

expect_exit 0 "$CCCC" -E -o t.i t.c
grep -q '__cccc_' t.i && fail "-E printed an internal template marker"
grep -q '^@template(T) T max' t.i || fail "-E lost @template"
grep -q 'max@(int)(40, 42)' t.i || fail "-E split or changed the instance"

expect_exit 42 "$CCCC" t.c
expect_exit 42 "$CCCC" t.i

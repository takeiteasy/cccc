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
    @inst(@inst(int) Box) Box b = {{1}};
    @inst(long, long) Tuple t = {1, 2};
    Tuple<int, int> u = {1, 2};
    return BIG(40, 42) - 2 + (t.v + ...) - 3 + a.v + b.v.v;
}
SRC

expect_exit 0 "$CCCC" -E -o t.i t.c
grep -q '__cccc_' t.i && fail "-E printed an internal template marker"
grep -q '^@template(T) T max' t.i || fail "-E lost @template"
grep -q '@inst(int) max(40, 42)' t.i || fail "-E split or changed the instance"
grep -q '@inst(@inst(int) Box) Box b' t.i || fail "-E changed a nested instance"
grep -q '@inst(long, long) Tuple t' t.i || fail "-E changed a multi-argument instance"
grep -q 'Tuple<int, int> u' t.i || fail "-E changed an angle instance"

expect_exit 42 "$CCCC" t.c
expect_exit 42 "$CCCC" t.i

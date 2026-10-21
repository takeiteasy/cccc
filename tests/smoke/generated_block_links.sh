# A block literal lifted while building a macro-generated function body is
# emitted by -c=generated, and the output links against a hand-written main.
. "$(dirname "$0")/lib.sh"

cat >gen.c <<'EOS'
[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("use_block", GetType("int"));
    FunctionSetBody(fn, Quote(
        "{ int n = 42; int (^b)(void) = ^{ return n; }; return b(); }"
    ));
    PublishNode(fn);
}
gen();

int use_block(void);

int main(void) {
    return use_block();
}
EOS
cat >harness.c <<'EOS'
int use_block(void);
int main(void) {
    return use_block();
}
EOS

cc=$(real_cc)
expect_exit 0 "$CCCC" -c=generated -o gen.gen.c gen.c
n=$(grep -o __cccc_block_0 gen.gen.c | wc -l)
[ "$n" -ge 3 ] || fail "-c=generated output does not define __cccc_block_0 ($n occurrences)"
expect_exit 0 "$cc" -c gen.gen.c -o gen.o
expect_exit 0 "$cc" -c harness.c -o harness.o
expect_exit 0 "$cc" -o prog gen.o harness.o
expect_exit 42 ./prog

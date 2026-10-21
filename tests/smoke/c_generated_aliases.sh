# -c=generated with no -o writes ./a.gen.c; -cgen, -cg and --compile=generated
# alias it; -G is gone; --emit-only and --attr-target are accepted.
. "$(dirname "$0")/lib.sh"

cat >gen.c <<'EOS'
[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("c_generated_answer", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
    PublishNode(fn);
}
gen();
int main(void) { return c_generated_answer(); }
EOS

rm -f a.gen.c
expect_exit 0 "$CCCC" -c=generated gen.c
[ -f a.gen.c ] || fail "./a.gen.c was not written"
grep -q c_generated_answer a.gen.c || fail "a.gen.c is missing the generated function"

for alias in -cgen -cg --compile=generated; do
    expect_exit 0 "$CCCC" "$alias" -o alias.c gen.c
    cmp -s alias.c a.gen.c || fail "$alias output differs from -c=generated's"
done

expect_fail "$CCCC" -G gen.c
expect_exit 0 "$CCCC" -c=generated --attr-target=gnu --emit-only -o attr.c gen.c

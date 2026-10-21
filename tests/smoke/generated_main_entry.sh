# A macro-generated main() is the entry point under the VM, -c=native and
# -c=generated.
. "$(dirname "$0")/lib.sh"

cat >gen_main.c <<'EOS'
[[cccc::comptime]]
void gen_program(void) {
    Obj *m = MakeFunction("main", GetType("int"));
    WithFn(m) {
        FunctionSetBody(m, Quote("{ int n = 0; for (int i = 0; i < 7; i++) n += 6; return n; }"));
    }
}
gen_program();
EOS

cc=$(real_cc)
expect_exit 42 "$CCCC" gen_main.c
expect_exit 0 "$CCCC" -c=native -o native_out gen_main.c
expect_exit 42 ./native_out
expect_exit 0 "$CCCC" -c=generated -o gen_main.gen.c gen_main.c
grep -q 'int main(void)' gen_main.gen.c || fail "-c=generated output lacks main"
expect_exit 0 "$cc" -o gen_out gen_main.gen.c
expect_exit 42 ./gen_out

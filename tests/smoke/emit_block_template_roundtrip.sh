# An emit block holding @template / @inst is replayed as written by
# -c=generated, and the output compiles again.
. "$(dirname "$0")/lib.sh"

cat >emit_tmpl.c <<'EOF'
#pragma cccc comptime begin
#pragma cccc emit begin
@template(T)
T mx(T a, T b) {
    return a > b ? a : b;
}
int answer(void) {
    return @inst(int) mx(1, 42);
}
#pragma cccc emit end
#pragma cccc comptime end

int main(void) { return answer(); }
EOF

expect_exit 42 "$CCCC" emit_tmpl.c
expect_exit 0 "$CCCC" -c=generated -o emit_tmpl.gen.c emit_tmpl.c
grep -q '@inst(int) mx(1, 42)' emit_tmpl.gen.c || fail "-c=generated lost the @inst line"
printf 'int main(void) { return answer(); }\n' >>emit_tmpl.gen.c
expect_exit 42 "$CCCC" emit_tmpl.gen.c

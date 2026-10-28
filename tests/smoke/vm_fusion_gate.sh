# The fused indexed load/store and multiply-add opcodes are emitted by default
# and never under a flag whose checks they would skip.
. "$(dirname "$0")/lib.sh"

cat >prog.c <<'EOF'
struct S { int pad; int arr[8]; };
int main(void) {
    int a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    double d[4] = {1.5, 2.5, 3.5, 4.5};
    struct S s = {0, {0}};
    int sum = 0, chk = 0;
    double acc = 0.0;
    for (int i = 0; i < 4; i++) {
        sum = sum + a[i] * a[i + 1];
        acc = acc + d[i] * d[i];
        s.arr[i] = a[i] * 7;
        chk = chk + i * 3;
    }
    return sum == 40 && acc == 41.0 && s.arr[3] == 28 && chk == 18 ? 42 : 1;
}
EOF
FUSED='(LDR|STR)_INDEX_|FLDR_INDEX|FSTR_INDEX|MULADD3|MULADDI3|MULI3|FMADD3'

expect_exit 42 "$CCCC" prog.c
expect_exit 0 "$CCCC" -d prog.c
for op in LDR_INDEX_W STR_INDEX_W FLDR_INDEX MULADD3 MULADDI3 MULI3 FMADD3; do
    grep -q "$op" out.txt || fail "$op not emitted by default"
done

for flag in -B --checked-pointers --uaf-detection --type-checks \
    --uninitialized-detection --overflow-checks --stack-instrumentation \
    --dangling-pointers --provenance-tracking --invalid-arithmetic -1 -2 -3; do
    expect_exit 42 "$CCCC" "$flag" prog.c
    expect_exit 0 "$CCCC" "$flag" -d prog.c
    ! grep -Eq "$FUSED" out.txt || fail "$flag still emits a fused opcode"
done

cat >oob.c <<'EOF'
int main(void) {
    int a[4] = {1, 2, 3, 4};
    volatile int i = 9;
    a[i] = 1;
    return a[i] + 42;
}
EOF
expect_fail "$CCCC" -B oob.c

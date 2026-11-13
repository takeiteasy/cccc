# -c=generated writes the float-to-integer helpers a generated function's cast
# calls, and only those, and the output builds with the host compiler.
. "$(dirname "$0")/lib.sh"

CC=$(real_cc)

# check NAME HELPER: build NAME.c with -c=generated, link a plain-C main and
# expect 42 and exactly one definition of HELPER.
check() {
    expect_exit 0 "$CCCC" -c=generated -o "$1.gen.c" "$1.c"
    printf '%s\n' "$MAIN" >>"$1.gen.c"
    "$CC" -w -Werror=implicit-function-declaration -o "$1" "$1.gen.c" ||
        fail "$1: host compiler rejected the output"
    expect_exit 42 "./$1"
    defs=$(grep -c "^static .* $2(.*) {" "$1.gen.c")
    [ "$defs" -eq 1 ] || fail "$1: $defs definitions of $2, expected 1"
}

MAIN='int main(void) { return conv(21.0) + 0; }'

cat >f64.c <<'EOF2'
@comptime void gen(void) {
    Obj *fn = MakeFunction("conv", GetType("int"));
    FunctionAddParam(fn, "x", GetType("double"));
    WithFn(fn) {
        Node *x = MakeParamRef(fn, "x");
        FunctionSetBody(fn, MakeReturn(MakeCast(MakeBinary(NK_MUL, x, MakeFloatLiteral(2.0)), GetType("int"))));
    }
}
gen();
int main(void) { return conv(21.0); }
EOF2
check f64 __cccc_f2i64

MAIN='int main(void) { return conv(42.0f); }'
cat >f32.c <<'EOF2'
@comptime void gen(void) {
    Obj *fn = MakeFunction("conv", GetType("int"));
    FunctionAddParam(fn, "x", GetType("float"));
    WithFn(fn) {
        Node *x = MakeParamRef(fn, "x");
        FunctionSetBody(fn, MakeReturn(MakeCast(x, GetType("int"))));
    }
}
gen();
int main(void) { return conv(42.0f); }
EOF2
check f32 __cccc_f2i64_f32

MAIN='int main(void) { return (int)conv(21.0); }'
cat >u64.c <<'EOF2'
@comptime void gen(void) {
    Obj *fn = MakeFunction("conv", GetType("unsigned long long"));
    FunctionAddParam(fn, "x", GetType("double"));
    WithFn(fn) {
        Node *x = MakeParamRef(fn, "x");
        FunctionSetBody(fn, MakeReturn(MakeCast(MakeBinary(NK_MUL, x, MakeFloatLiteral(2.0)), GetType("unsigned long long"))));
    }
}
gen();
int main(void) { return (int)conv(21.0); }
EOF2
check u64 __cccc_f2u64

# A float cast only in user code is not generated output: no helper.
cat >user_only.c <<'EOF2'
@comptime void gen(void) {
    Obj *fn = MakeFunction("conv", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
}
gen();
int user(double x) { return (int)x; }
int main(void) { return conv() + user(1.5) - 1; }
EOF2
expect_exit 0 "$CCCC" -c=generated -o user_only.gen.c user_only.c
! grep -q "__cccc_f2i" user_only.gen.c || fail "user-only cast pulled in a helper"

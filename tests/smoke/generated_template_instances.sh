# -c=generated writes the template function instances that generated code
# uses, and the output builds with the host compiler.
. "$(dirname "$0")/lib.sh"

CC=$(real_cc)

# check NAME INSTANCE: build NAME.c with -c=generated, link a plain-C main that
# calls ten(), and expect 42 and exactly one definition of INSTANCE.
check() {
    expect_exit 0 "$CCCC" -c=generated -o "$1.gen.c" "$1.c"
    printf '%s\n' "$MAIN" >>"$1.gen.c"
    "$CC" -w -o "$1" "$1.gen.c" || fail "$1: host compiler rejected the output"
    expect_exit 42 "./$1"
    defs=$(grep -c "^static .* $2(.*) {" "$1.gen.c")
    [ "$defs" -eq 1 ] || fail "$1: $defs definitions of $2, expected 1"
}

MAIN='int main(void) { return ten() + 32; }'

cat >file_scope.c <<'EOF'
@template(T) T twice(T a) { return a + a; }
@comptime void gen(void) {
    InstantiateFunction("twice", "int");
    Obj *ten = MakeFunction("ten", GetType("int"));
    FunctionSetBody(ten, MakeReturn(Quote("twice_int(5)")));
}
gen();
int main(void) { return ten() + 32; }
EOF
check file_scope twice_int

cat >in_function.c <<'EOF'
@template(T) T twice(T a) { return a + a; }
@comptime Node *gen(void) {
    InstantiateFunction("twice", "int");
    Obj *ten = MakeFunction("ten", GetType("int"));
    FunctionSetBody(ten, MakeReturn(Quote("twice_int(5)")));
    return MakeIntLiteral(0);
}
int ten(void);
int main(void) { return gen() + ten() + 32; }
EOF
check in_function twice_int

cat >quote_inst.c <<'EOF'
@template(T) T twice(T a) { return a + a; }
@comptime void gen(void) {
    Obj *ten = MakeFunction("ten", GetType("int"));
    FunctionSetBody(ten, MakeReturn(Quote("@inst(int) twice(5)")));
}
gen();
int main(void) { return ten() + 32; }
EOF
check quote_inst twice_int

# An instance reached only through another instance.
cat >nested.c <<'EOF'
@template(T) T twice(T a) { return a + a; }
@template(T) T quad(T a) { return twice @(T)(twice @(T)(a)); }
@comptime void gen(void) {
    Obj *ten = MakeFunction("ten", GetType("int"));
    FunctionSetBody(ten, MakeReturn(Quote("@inst(int) quad(1) + 6")));
}
gen();
int main(void) { return ten() + 32; }
EOF
check nested quad_int
grep -q "^static .* twice_int(.*) {" nested.gen.c || fail "nested: twice_int missing"

# An instance whose parameter type is itself an instance.
cat >type_param.c <<'EOF'
@template(T) struct Box { T value; };
@template(T) T unbox(Box @(T) b) { return b.value; }
@comptime void gen(void) {
    Obj *ten = MakeFunction("ten", GetType("int"));
    FunctionSetBody(ten, MakeReturn(Quote("({ Box @(int) b = {10}; @inst(int) unbox(b); })")));
}
gen();
int main(void) { return ten() + 32; }
EOF
check type_param unbox_int

# A bound float parameter becomes a constant the instance reads.
cat >float_param.c <<'EOF'
@template(double V) double scale(double x) { return x * V; }
@comptime void gen(void) {
    Obj *ten = MakeFunction("ten", GetType("double"));
    FunctionSetBody(ten, MakeReturn(Quote("@inst(2.5) scale(4.0)")));
}
gen();
int main(void) { return (int)ten() + 32; }
EOF
MAIN='double ten(void); int main(void) { return (int)ten() + 32; }'
check float_param scale_2p5
grep -q "__cccc_tp_scale_2p5__V = 2.5" float_param.gen.c || fail "float_param: constant missing"

# Instances nothing generated uses stay out of the output.
cat >unused.c <<'EOF'
@template(T) T twice(T a) { return a + a; }
@template(T) T thrice(T a) { return a + a + a; }
@comptime void gen(void) {
    InstantiateFunction("twice", "int");
    Obj *ten = MakeFunction("ten", GetType("int"));
    FunctionSetBody(ten, MakeReturn(Quote("twice_int(5)")));
}
gen();
int main(void) { return ten() + thrice @(int)(1) + 29; }
EOF
MAIN='int main(void) { return ten() + 32; }'
check unused twice_int
! grep -q thrice unused.gen.c || fail "unused: an instance generated code never calls was written"

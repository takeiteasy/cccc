# Each input's file-scope generator calls declare their names in a scope of
# their own: another input does not see them, and the linker treats what they
# generate like the inputs' own definitions.
. "$(dirname "$0")/lib.sh"

cat >tmpl.h <<'EOS'
@template(T) struct Box {
    T value;
};
@template(T) T twice(T a) {
    return a + a;
}
EOS

# One template, the same instances made by both inputs' calls.
cat >a.c <<'EOS'
#include "tmpl.h"
@comptime void gen_a(void) {
    Type *box = InstantiateType("Box", "int");
    InstantiateFunction("twice", "int");
    Obj *fn = MakeFunction("size_a", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(TypeSize(box))));
}
gen_a();
int from_b(void);
int main(void) {
    Box @(int) b = {10};
    return size_a() + from_b() + twice@(int)(b.value);
}
EOS
cat >b.c <<'EOS'
#include "tmpl.h"
@comptime void gen_b(void) {
    Type *box = InstantiateType("Box", "int");
    InstantiateFunction("twice", "int");
    Obj *fn = MakeFunction("size_b", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(TypeSize(box))));
}
gen_b();
int from_b(void) {
    Box @(int) c = {1};
    return size_b() + twice@(int)(c.value) + 12;
}
EOS
expect_exit 42 "$CCCC" a.c b.c
expect_exit 0 "$CCCC" -c=native -o same_out a.c b.c
expect_exit 42 ./same_out

# Two different templates of one name, each input's own.
cat >c.c <<'EOS'
@template(T) struct Box {
    T value;
};
@comptime void gen_c(void) {
    Type *box = InstantiateType("Box", "int");
    Obj *fn = MakeFunction("size_c", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(TypeSize(box))));
}
gen_c();
int from_d(void);
int main(void) {
    Box @(int) b = {10};
    return size_c() + b.value + from_d();
}
EOS
cat >d.c <<'EOS'
@template(T) struct Box {
    T value;
    T other;
};
@comptime void gen_d(void) {
    Type *box = InstantiateType("Box", "int");
    Obj *fn = MakeFunction("size_d", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(TypeSize(box))));
}
gen_d();
int from_d(void) {
    Box @(int) b = {1, 2};
    return size_d() + b.value + b.other + 17;
}
EOS
expect_exit 42 "$CCCC" c.c d.c
expect_exit 0 "$CCCC" -c=native -o diff_out c.c d.c
expect_exit 42 ./diff_out

# Two templates of one name that make an instance of the same name as functions
# cannot both be linked.
cat >e.c <<'EOS'
@template(T) T twice(T a) { return a + a; }
@comptime void gen_e(void) { InstantiateFunction("twice", "int"); }
gen_e();
int from_f(void);
int main(void) { return twice@(int)(1) + from_f(); }
EOS
cat >f.c <<'EOS'
@template(T) T twice(T a) { return a * 2 + 1; }
@comptime void gen_f(void) { InstantiateFunction("twice", "int"); }
gen_f();
int from_f(void) { return twice@(int)(1); }
EOS
expect_fail "$CCCC" e.c f.c
grep -q "conflicting definitions of template instance 'twice_int'" err.txt ||
    fail "no template instance conflict: $(cat err.txt)"

# A type a call makes is not visible in another input.
cat >g.c <<'EOS'
@comptime void gen_g(void) { MakeTypedef("MyInt", GetType("int")); }
gen_g();
int from_h(void);
int main(void) { MyInt x = 40; return x + from_h(); }
EOS
cat >h.c <<'EOS'
int from_h(void) { return 2; }
EOS
expect_exit 42 "$CCCC" g.c h.c
cat >h_bad.c <<'EOS'
int from_h(void) { MyInt y = 2; return y; }
EOS
expect_fail "$CCCC" g.c h_bad.c
grep -q "MyInt" err.txt || fail "MyInt not reported: $(cat err.txt)"

# A generated function is declared in the input whose call made it only.
cat >k.c <<'EOS'
@comptime void gen_k(void) {
    Obj *fn = MakeFunction("made_in_k", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(40)));
}
gen_k();
int from_l(void);
int main(void) { return made_in_k() + from_l() - 40; }
EOS
cat >l.c <<'EOS'
int from_l(void) { return made_in_k() + 2; }
EOS
expect_fail "$CCCC" k.c l.c
grep -q "call to undeclared function 'made_in_k'" err.txt ||
    fail "no undeclared-function error: $(cat err.txt)"
cat >l_proto.c <<'EOS'
int made_in_k(void);
int from_l(void) { return made_in_k() + 2; }
EOS
expect_exit 42 "$CCCC" k.c l_proto.c

# A generated signature naming the generator's own typedef is not prepended to
# an input that cannot spell it.
cat >m.c <<'EOS'
@comptime void gen_m(void) {
    Type *t = MakeTypedef("MyLong", GetType("long"));
    Obj *fn = MakeFunction("make_m", t);
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(40)));
}
gen_m();
int from_n(void);
int main(void) { MyLong v = make_m(); return (int)v + from_n(); }
EOS
cat >n.c <<'EOS'
int from_n(void) { return 2; }
EOS
expect_exit 42 "$CCCC" m.c n.c

# Both inputs generate `helper`.
for x in p q; do
    {
        echo "@comptime void gen_$x(void) {"
        echo '    Obj *fn = MakeFunction("helper", GetType("int"));'
        [ $x = p ] && echo '    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(40)));'
        [ $x = q ] && echo '    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(2)));'
        echo '}'
        echo "gen_$x();"
        [ $x = p ] && echo 'int from_q(void); int main(void) { return helper() + from_q(); }'
        [ $x = q ] && echo 'int from_q(void) { return helper(); }'
        echo "int helper(void);"
    } >dup_$x.c
done
expect_fail "$CCCC" dup_p.c dup_q.c
grep -q "redefinition of 'helper'" err.txt ||
    fail "no duplicate-name error: $(cat err.txt)"

# Two external globals of one name are a link error as well.
for x in p q; do
    cat >dupg_$x.c <<EOS
@comptime void gen_$x(void) { GlobalVar("counter", GetType("int")); }
gen_$x();
EOS
done
echo 'int main(void) { return 0; }' >>dupg_p.c
expect_fail "$CCCC" dupg_p.c dupg_q.c
grep -q "redefinition of 'counter'" err.txt ||
    fail "no duplicate-global error: $(cat err.txt)"

# One input cannot generate a name twice.
cat >twice.c <<'EOS'
@comptime void gen1(void) { MakeFunction("dup", GetType("int")); }
@comptime void gen2(void) { MakeFunction("dup", GetType("int")); }
gen1();
gen2();
int main(void) { return 0; }
EOS
expect_fail "$CCCC" twice.c
grep -q "expected unique generated function name" err.txt ||
    fail "no same-input duplicate error: $(cat err.txt)"
cat >twiceg.c <<'EOS'
@comptime void gen1(void) { GlobalVar("g", GetType("int")); }
@comptime void gen2(void) { GlobalVar("g", GetType("int")); }
gen1();
gen2();
int main(void) { return 0; }
EOS
expect_fail "$CCCC" twiceg.c
grep -q "expected unique generated global name" err.txt ||
    fail "no same-input duplicate global error: $(cat err.txt)"

# A generated static is private to the input whose call made it.
cat >s_one.c <<'EOS'
@comptime void gen(void) {
    Obj *fn = MakeFunction("helper", GetType("int"));
    FunctionSetStatic(fn, true);
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
}
gen();
int main(void) { return helper(); }
EOS
expect_exit 42 "$CCCC" s_one.c
for x in p q; do
    v=40
    [ $x = q ] && v=2
    cat >st_$x.c <<EOS
@comptime void gen_$x(void) {
    Obj *fn = MakeFunction("helper", GetType("int"));
    FunctionSetStatic(fn, true);
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral($v)));
    Obj *v = GlobalVar("counter", GetType("int"));
    GlobalVarSetStatic(v, true);
}
gen_$x();
EOS
done
echo 'int from_q(void); int main(void) { counter = 40; return helper() + from_q() + counter; }' >>st_p.c
echo 'int from_q(void) { counter = 2; return counter - 2; }' >>st_q.c
expect_exit 80 "$CCCC" st_p.c st_q.c
expect_exit 0 "$CCCC" -c=native -o st_out st_p.c st_q.c
expect_exit 80 ./st_out

# An external generated function and a static one of the same name.
cat >mx_p.c <<'EOS'
@comptime void gen_p(void) {
    Obj *fn = MakeFunction("helper", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(40)));
}
gen_p();
int from_q(void);
int main(void) { return helper() + from_q(); }
EOS
cat >mx_q.c <<'EOS'
@comptime void gen_q(void) {
    Obj *fn = MakeFunction("helper", GetType("int"));
    FunctionSetStatic(fn, true);
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(2)));
}
gen_q();
int from_q(void) { return helper(); }
EOS
expect_exit 42 "$CCCC" mx_p.c mx_q.c
expect_exit 0 "$CCCC" -c=native -o mx_out mx_p.c mx_q.c
expect_exit 42 ./mx_out
expect_exit 0 "$CCCC" -c=native -o mx_out2 mx_q.c mx_p.c
expect_exit 42 ./mx_out2

# Both inputs include one header that declares a function and each call builds
# a call to it.
cat >fn_h.h <<'EOS'
int twice(int);
EOS
cat >fn_a.c <<'EOS'
#include "fn_h.h"
@comptime void gen_fa(void) {
    Obj *fn = MakeFunction("fa", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeFuncCall(MakeVarRef("twice"), (Node *[]){MakeIntLiteral(20)}, 1)));
}
gen_fa();
int fb(void);
int twice(int x) { return x + x; }
int main(void) { return fa() + fb() + 2 - 40; }
EOS
cat >fn_b.c <<'EOS'
#include "fn_h.h"
@comptime void gen_fb(void) {
    Obj *fn = MakeFunction("fb", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeFuncCall(MakeVarRef("twice"), (Node *[]){MakeIntLiteral(20)}, 1)));
}
gen_fb();
int fa(void);
EOS
expect_exit 42 "$CCCC" fn_a.c fn_b.c
expect_exit 0 "$CCCC" -c=native -o fn_out fn_a.c fn_b.c
expect_exit 42 ./fn_out

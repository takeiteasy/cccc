# Two inputs include one template header and each runs a file-scope generator
# that instantiates its templates; neither sees a clash with the other.
. "$(dirname "$0")/lib.sh"

cat >tmpl.h <<'EOS'
@template(T) struct Box {
    T value;
};
@template(T) T twice(T a) {
    return a + a;
}
EOS

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
    return size_a() + from_b() + twice@(int)(b.value) + 17;
}
EOS

cat >b.c <<'EOS'
#include "tmpl.h"
@comptime void gen_b(void) {
    Type *box = InstantiateType("Box", "char");
    InstantiateFunction("twice", "long");
    Obj *fn = MakeFunction("size_b", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(TypeSize(box))));
}
gen_b();
int from_b(void) {
    Box @(char) c = {1};
    return size_b() + twice@(long)(c.value) - 2;
}
EOS

expect_exit 42 "$CCCC" a.c b.c
expect_exit 0 "$CCCC" -c=native -o two_out a.c b.c
expect_exit 42 ./two_out

# Both inputs declare the same Point, instantiate over it from a call and name
# the instance themselves; the instances link into one.
cat >pt.h <<'EOS'
typedef struct {
    int a;
    int b;
} Point;
@template(T) struct Wrap {
    T value;
};
EOS

cat >c.c <<'EOS'
#include "pt.h"
@comptime void gen_c(void) {
    InstantiateType("Wrap", "Point");
}
gen_c();
int from_d(void);
int main(void) {
    Wrap @(Point) w = {{20, 2}};
    Point         p = w.value;
    return p.a + p.b + from_d() + 19;
}
EOS

cat >d.c <<'EOS'
#include "pt.h"
@comptime void gen_d(void) {
    InstantiateType("Wrap", "Point");
}
gen_d();
int from_d(void) {
    Wrap @(Point) w = {{10, 11}};
    return w.value.a + w.value.b - 20;
}
EOS

expect_exit 42 "$CCCC" c.c d.c
expect_exit 0 "$CCCC" -c=native -o point_out c.c d.c
expect_exit 42 ./point_out

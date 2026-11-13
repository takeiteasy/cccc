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

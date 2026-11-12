# A function template instance is a weak symbol in each separately compiled
# object, so linking the objects merges the copies into one; one over a
# block-scope type is private to its object.
. "$(dirname "$0")/lib.sh"

cat >vec.h <<'EOS'
@template(T) T max(T a, T b) { return a > b ? a : b; }
@template(T) int size_of(void) { return sizeof(T); }
EOS
cat >a.c <<'EOS'
#include "vec.h"
int (*b_max(void))(int, int);
int local_size(void) {
    struct S { char x[3]; };
    return @inst(struct S) size_of();
}
int main(void) {
    return b_max() == &@inst(int) max && local_size() == 3
               ? @inst(int) max(42, 1)
               : 1;
}
EOS
cat >b.c <<'EOS'
#include "vec.h"
int (*b_max(void))(int, int) {
    return &@inst(int) max;
}
EOS

"$CCCC" -c=object a.c -o a.o || fail "compiling a.c failed"
"$CCCC" -c=object b.c -o b.o || fail "compiling b.c failed"
# `nm -m` spells it on macOS, a W/w type letter elsewhere.
{ nm -m a.o 2>/dev/null; nm a.o; } | grep -q 'weak external _max__int\| [Ww] max__int$' ||
    fail "max__int is not weak in a.o"
{ nm -m a.o 2>/dev/null; nm a.o; } | grep 'size_of__struct_S__in_local_size' | grep -q 'weak\| [Ww] ' &&
    fail "size_of__struct_S__in_local_size is weak in a.o"
expect_exit 0 cc a.o b.o -o prog
expect_exit 42 ./prog

# A system header macro that expands to a member path ending in its own name
# (glibc's `sa_handler`) must not be expanded a second time by the host cc
# when the serialized program is built with -c=native.
. "$(dirname "$0")/lib.sh"

mkdir inc
cat >inc/alias.h <<'H'
#ifndef ALIAS_H
#define ALIAS_H
struct S {
    int pad;
    union { int v; long w; } u;
};
#define v u.v
#endif
H
cat >prog.c <<'C'
#include <alias.h>
struct S g = {.v = 40};
int main(void) {
    struct S s = {.v = 1};
    struct S *p = &s;
    p->v += 1;
    return g.v + s.v;
}
C

expect_exit 0 "$CCCC" -i inc --use-system-headers -c=native -o prog prog.c
expect_exit 42 ./prog

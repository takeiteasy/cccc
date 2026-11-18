# `overloadable` functions get the symbols Clang gives them, so each side links
# against the other's: cccc calls Clang-built overloads, and Clang's symbol
# list for the same source matches cccc's.
. "$(dirname "$0")/lib.sh"

CC=$(real_cc)
[ "$(cc_family "$CC")" = clang ] || { echo "skip: the host cc is not clang"; exit 0; }

cat >overloads.h <<'H'
#define OVL __attribute__((overloadable))
int OVL pick(int x);
int OVL pick(double x);
int OVL pick(const char *s, const char *t);
int OVL pick(int *a, int *b);
int OVL pick(long long x);
H

cat >lib.c <<'H'
#include "overloads.h"
int OVL pick(int x) { return 1; }
int OVL pick(double x) { return 2; }
int OVL pick(const char *s, const char *t) { return 3; }
int OVL pick(int *a, int *b) { return 4; }
int OVL pick(long long x) { return 5; }
H

cat >main.c <<'H'
#include "overloads.h"
int main(void) {
    int i = 0;
    int (*by_double)(double) = pick;
    if (by_double(1.0) != 2)
        return 2;
    return pick(0) * 10000 + pick(1.0) * 1000 + pick("a", "b") * 100 +
           pick(&i, &i) * 10 + pick(1LL) == 12345 ? 42 : 1;
}
H

"$CC" -c lib.c -o lib.o || fail "clang could not build the library"

# cccc calls what Clang built.
expect_exit 0 "$CCCC" -c=native --link-arg lib.o -o calls_clang main.c
expect_exit 42 ./calls_clang

# cccc's objects define the symbols Clang's do.
expect_exit 0 "$CCCC" -c=object -o lib_cccc.o lib.c
nm lib.o | awk '$2 == "T" { print $3 }' | sort >clang.syms
nm lib_cccc.o | awk '$2 == "T" { print $3 }' | sort >cccc.syms
[ -s clang.syms ] || fail "no symbols from clang"
cmp -s clang.syms cccc.syms || {
    diff clang.syms cccc.syms >&2
    fail "symbol lists differ"
}

# Clang calls what cccc built.
"$CC" main.c lib_cccc.o -o calls_cccc || fail "clang could not link cccc's object"
expect_exit 42 ./calls_cccc

# -E --output-compiler: the dump compiles under the named compiler with no
# attribute warnings, and cccc's __has_attribute under each target agrees
# with the real compiler for every GNU registry row. __has_builtin agrees for
# every builtin in the builtin registry, the attributes cccc only reports
# (CLANG_GNU_REPORT_ROW) are absent from gcc, a nested function (gcc) or
# block (clang) dump compiles under its compiler, and a <stdatomic.h> dump
# runs under it.
SRC=$(cd "$(dirname "$0")/../../src" && pwd)
REGISTRY=$SRC/attr_registry.c
BUILTIN_REGISTRY=$SRC/builtin_registry.c
. "$(dirname "$0")/lib.sh"

cat >in.c <<'EOS'
__attribute__((overloadable)) int f(int);
int h(int n) __attribute__((enable_if(n > 0, "p")));
void s(void *const p __attribute__((pass_object_size(0))));
__attribute__((optimize("O2"), cold)) void k(void);
__attribute__((unavailable)) void u(void);
struct __attribute__((packed, designated_init)) S { char c; int i; };
[[nodiscard]] int g(void);
EOS

# GNU rows plus optimize, the one CCCC row -E keeps. Standard rows are left
# out: gcc's __has_attribute also reports [[ ]]-only names, clang's does not.
grep -oE '(GNU_ROW|GCC_ROW|CLANG_ROW|CLANG_GNU_ROW)\("[a-z_]+"|\{"(optimize|unavailable)"' \
    "$REGISTRY" | sed -E 's/.*"([a-z_]+)"/\1/' >names.txt
[ -s names.txt ] || fail "no registry rows in $REGISTRY"
while read -r n; do
    printf '#if __has_attribute(%s)\n%s 1\n#else\n%s 0\n#endif\n' "$n" "$n" "$n"
done <names.txt >has.c

grep -oE '^    \{"[A-Za-z0-9_]+"' "$BUILTIN_REGISTRY" | sed -E 's/.*"(.*)"/\1/' >builtins.txt
[ -s builtins.txt ] || fail "no builtin rows in $BUILTIN_REGISTRY"
while read -r n; do
    printf '#if __has_builtin(%s)\n%s 1\n#else\n%s 0\n#endif\n' "$n" "$n" "$n"
done <builtins.txt >hasb.c

grep -oE 'CLANG_GNU_REPORT_ROW\("[a-z_]+"' "$REGISTRY" | sed -E 's/.*"(.*)"/\1/' >report.txt
[ -s report.txt ] || fail "no report-only rows in $REGISTRY"
while read -r n; do
    printf '#if __has_attribute(%s)\n%s 1\n#else\n%s 0\n#endif\n' "$n" "$n" "$n"
done <report.txt >hasr.c

cat >atomics.c <<'EOS'
#include <stdatomic.h>
int a[4];
int *_Atomic p = a;
long _Atomic n;
int main(void) {
    atomic_fetch_add_explicit(&p, 2, memory_order_seq_cst);
    atomic_fetch_sub_explicit(&p, 1, memory_order_seq_cst);
    atomic_fetch_add(&n, 5);
    atomic_fetch_sub(&n, 2);
    atomic_fetch_or(&n, 8);
    atomic_fetch_and(&n, 0xf);
    atomic_fetch_xor(&n, 1);
    atomic_thread_fence(memory_order_seq_cst);
    atomic_signal_fence(memory_order_seq_cst);
    return p - a == 1 && n == 10 ? 42 : 1;
}
EOS

cat >nested.c <<'EOS'
int f(int x) {
    int g(int y) { return x + y; }
    return g(1);
}
EOS
cat >blocks.c <<'EOS'
int f(void) {
    int (^b)(int) = ^(int y) { return y + 1; };
    return b(1);
}
EOS

check() {
    target=$1
    cc=$2
    flags=$3
    for at in auto c23; do
        "$CCCC" -E --attr-target=$at --output-compiler=$target \
            -Wno-target-compiler in.c >out_$at.c ||
            fail "-E --output-compiler=$target failed"
        expect_exit 0 "$cc" -std=c2x -fsyntax-only $flags out_$at.c
        [ -s err.txt ] && { cat err.txt >&2; fail "$target warned on $at output"; }
    done
    # cccc reads its own output back without an attribute warning.
    { cat out_c23.c; echo 'int main(void) { return 42; }'; } >back.c
    expect_exit 42 "$CCCC" back.c
    grep -q 'Wunknown-attributes\|Wattributes' err.txt &&
        { cat err.txt >&2; fail "cccc warned reading back $target output"; }
    if [ "$target" = gcc ]; then src=nested.c; else src=blocks.c; fi
    "$CCCC" -E --output-compiler=$target $src >syntax_out.c ||
        fail "-E --output-compiler=$target rejected $src"
    [ "$target" = clang ] && blocks=-fblocks || blocks=
    expect_exit 0 "$cc" $blocks -fsyntax-only syntax_out.c
    # The mask matches gcc 12+ (unavailable) and clang 14+ (error, warning).
    major=$("$cc" -dM -E - </dev/null |
        sed -n "s/^#define __${4}__ //p")
    if [ "${major:-0}" -lt "$5" ]; then
        echo "skip: $target $major predates the registry mask"
        return
    fi
    # Builtins cccc alone has read 0 under --output-compiler; the compilers
    # lack them too.
    "$CCCC" -E -P -Wno-target-compiler --output-compiler=$target hasb.c | grep . >cccc_b.txt
    "$cc" -E -P -x c hasb.c | grep . >real_b.txt
    diff real_b.txt cccc_b.txt >&2 || fail "__has_builtin differs from $target"

    # Attributes cccc only reports are absent from gcc; cccc itself reads 0.
    "$CCCC" -E -P --output-compiler=$target hasr.c | grep . >cccc_r.txt
    grep -q ' 1$' cccc_r.txt && fail "cccc claims a report-only attribute"
    if [ "$target" = gcc ]; then
        "$cc" -E -P -x c hasr.c | grep . >real_r.txt
        grep -q ' 1$' real_r.txt && { cat real_r.txt >&2; fail "gcc has a report-only attribute"; }
    elif [ "${major:-0}" -ge 16 ]; then
        "$cc" -E -P -x c hasr.c | grep . >real_r.txt
        grep -q ' 0$' real_r.txt && { cat real_r.txt >&2; fail "clang lacks a report-only attribute"; }
    fi

    # The <stdatomic.h> dump builds and runs under the named compiler.
    "$CCCC" --std=c11 -E --output-compiler=$target atomics.c >atomics_out.c ||
        fail "-E --output-compiler=$target rejected atomics.c"
    expect_exit 0 "$cc" -std=gnu11 -w -o atomics_bin atomics_out.c
    expect_exit 42 ./atomics_bin

    "$CCCC" -E -P --output-compiler=$target has.c | grep . >cccc.txt
    "$cc" -E -P -x c has.c | grep . >real.txt
    [ "$(wc -l <real.txt)" -eq "$(wc -l <names.txt)" ] || fail "short probe"
    diff real.txt cccc.txt >&2 || fail "__has_attribute differs from $target"

    # [[ ]] namespaces: every GNU and standard row, in each namespace.
    for ns in gnu clang; do
        while read -r n; do
            printf '#if __has_c_attribute(%s::%s)\n%s::%s 1\n#else\n%s::%s 0\n#endif\n' \
                $ns "$n" $ns "$n" $ns "$n"
        done <names.txt
    done >hasns.c
    "$CCCC" -E -P --output-compiler=$target hasns.c | grep . >cccc_ns.txt
    "$cc" -std=c2x -E -P -x c hasns.c | grep . >real_ns.txt
    diff real_ns.txt cccc_ns.txt >&2 ||
        fail "__has_c_attribute(ns::name) differs from $target"
}

checked=0
if clang=$(find_family clang clang cc); then
    check clang "$clang" "-Werror=ignored-attributes -Werror=unknown-attributes" \
        clang_major 14
    checked=1
fi
if gcc=$(find_family gcc gcc-16 gcc-15 gcc-14 gcc /opt/homebrew/bin/gcc-16); then
    check gcc "$gcc" "-Werror=attributes" GNUC 12
    checked=1
fi
[ "$checked" = 1 ] || echo "skip: no gcc or clang found"

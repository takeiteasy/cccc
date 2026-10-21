# The width-0 unnamed bit-field struct's layout guard against a real clang and
# a real gcc host: -c=native succeeds when the host lays the struct out as
# cccc folded it, and otherwise fails naming the type.
. "$(dirname "$0")/lib.sh"

cat >probe.c <<'EOS'
#include <stdio.h>
struct probe_zero_width_bitfield { char c; int : 0; char d; };
int main(void) {
    printf("%zu/%zu\n", sizeof(struct probe_zero_width_bitfield),
           _Alignof(struct probe_zero_width_bitfield));
    return 0;
}
EOS
cat >zw.c <<'EOS'
struct lg1172_zero_width_bitfield {
    char c;
    int : 0;
    char d;
};
struct lg1172_zero_width_bitfield g_zw;

int main(void) { return 42; }
EOS

expect_exit 0 "$CCCC" -m zw.c
size=$(sed -n 's/.*sizeof(struct lg1172_zero_width_bitfield) == \([0-9]*\).*/\1/p' out.txt | head -1)
align=$(sed -n 's/.*_Alignof(struct lg1172_zero_width_bitfield) == \([0-9]*\).*/\1/p' out.txt | head -1)
[ -n "$size" ] && [ -n "$align" ] || fail "cannot read the folded layout from -m output"
folded=$size/$align

# check_host LABEL CC
check_host() {
    expect_exit 0 "$2" -w -o probe_out probe.c
    expect_exit 0 ./probe_out
    actual=$(cat out.txt)
    if [ "$actual" = "$folded" ]; then
        CCCC_NATIVE_CC=$2 expect_exit 0 "$CCCC" -c=native -o zw_out zw.c
        expect_exit 42 ./zw_out
        echo "ok: $1 agrees with cccc's $folded"
    else
        CCCC_NATIVE_CC=$2 expect_fail "$CCCC" -c=native -o zw_out zw.c
        grep -q lg1172_zero_width_bitfield err.txt || {
            cat err.txt >&2
            fail "$1 lays the struct out as $actual (cccc folded $folded); the guard should name the type"
        }
        echo "ok: $1 lays it out as $actual (cccc folded $folded): the guard failed naming the type"
    fi
}

checked=0
if clang=$(find_family clang clang cc); then
    check_host clang "$clang"
    checked=1
fi
if gcc=$(find_family gcc gcc-16 gcc-15 gcc-14 gcc-13 gcc /opt/homebrew/bin/gcc-16); then
    check_host gcc "$gcc"
    checked=1
fi
[ "$checked" -eq 1 ] || echo "skip: no real clang or gcc on PATH"

# --compiler-family=auto resolves to the family of CCCC_NATIVE_CC.
. "$(dirname "$0")/lib.sh"

cat >prog.c <<'EOS'
// __CCCC_COMPILER_FAMILY__ is 0 (gcc policy) or 1 (clang policy); with
// --compiler-family=auto it must track CCCC_NATIVE_CC's own family.
#if __CCCC_COMPILER_FAMILY__ == 1
int main(void) { return 1; }   // clang policy resolved
#else
int main(void) { return 0; }   // gcc policy resolved
#endif
EOS

# check WANT CC: -m keeps only the branch the resolved family selects.
check() {
    CCCC_NATIVE_CC=$2 expect_exit 0 "$CCCC" --compiler-family=auto -m prog.c
    if [ "$1" = clang ]; then want='return 1;' other='return 0;'; else want='return 0;' other='return 1;'; fi
    grep -q "$want" out.txt && ! grep -q "$other" out.txt ||
        fail "auto did not resolve to $1 for $2"
}

checked=0
if cc=$(find_family clang clang cc); then check clang "$cc"; checked=1; fi
if cc=$(find_family gcc gcc-16 gcc-15 gcc-14 gcc-13 gcc /opt/homebrew/bin/gcc-16); then check gcc "$cc"; checked=1; fi
[ "$checked" -eq 1 ] || echo "skip: no real clang or gcc on PATH"

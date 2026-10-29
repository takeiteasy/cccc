# An undefined extern data object resolves from a -l / #pragma cccc link
# library, including a static initialiser that takes its address. A referenced
# symbol the library lacks is an error; an unreferenced declaration is not.
. "$(dirname "$0")/lib.sh"

cc=$(real_cc)
case $(uname -s) in
    Darwin) ext=dylib ;;
    *) ext=so ;;
esac

cat >lib.c <<'C'
int libvar = 7;
int libarr[4] = {1, 2, 3, 4};
int libget(void) { return libvar; }
C
"$cc" -shared -fPIC -o libdata."$ext" lib.c || fail "cannot build the test library"

cat >use.c <<'C'
extern int libvar;
extern int libarr[4];
extern int libget(void);
extern int never_used_anywhere;
static int *p = &libvar;
static int *q = &libarr[2];

int main(void) {
    if (libvar != 7)
        return 1;
    libvar = 40;
    if (libget() != 40)
        return 2;
    if (*p != 40)
        return 3;
    *p = 41;
    if (libvar != 41)
        return 4;
    if (*q != 3)
        return 5;
    return libarr[0] + libarr[3] + 37;
}
C
expect_exit 42 "$CCCC" --run -L . -l data use.c

{
    echo "#pragma cccc link(\"./libdata.$ext\")"
    cat use.c
} >pragma.c
expect_exit 42 "$CCCC" --run pragma.c

cat >missing.c <<'C'
extern int absent_symbol;
int main(void) { return absent_symbol; }
C
expect_fail "$CCCC" --run -L . -l data missing.c
grep -q "unresolved dynamic library symbol 'absent_symbol'" err.txt ||
    fail "missing symbol not reported"

cat >functions_only.c <<'C'
extern int libget(void);
extern int unreferenced_extern;
int main(void) { return libget() + 35; }
C
expect_exit 42 "$CCCC" --run -L . -l data functions_only.c

expect_exit 0 "$CCCC" -c=native -L . -l data -o nativeprog use.c
LD_LIBRARY_PATH=. DYLD_LIBRARY_PATH=. ./nativeprog
[ $? -eq 42 ] || fail "native build of use.c did not exit 42"

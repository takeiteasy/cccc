# A prototype written in an input file stays in the serialized program when
# another input's header declares the same function: the host cc may see that
# header under different feature macros, so it cannot be trusted to supply it.
. "$(dirname "$0")/lib.sh"

mkdir inc
echo 'int hdr_only_fn(int);' >inc/decl.h
printf '#include <decl.h>\nint main(void) { return 0; }\n' >a.c
printf 'extern int hdr_only_fn(int);\nint use(void) { return hdr_only_fn(1); }\n' >b.c

expect_exit 0 "$CCCC" -i inc --use-system-headers -m a.c b.c
grep -Eq '^int \(?hdr_only_fn\)?\(' out.txt || fail "input prototype was dropped"

# --test-run: a clean program compiles, an out-of-bounds write is refused at
# the default level, and --test-run=basic does not catch it.
. "$(dirname "$0")/lib.sh"

echo 'int main(void) { return 42; }' >clean.c
cat >oob.c <<'EOS'
#include <stdlib.h>
int main(void) {
    int *p = malloc(sizeof(int) * 4);
    p[10] = 5;
    free(p);
    return 42;
}
EOS

expect_exit 0 "$CCCC" --test-run -o clean_out clean.c
[ -f clean_out ] || fail "artifact was not written"
expect_exit 42 ./clean_out

expect_fail "$CCCC" --test-run -o oob_out oob.c
[ ! -e oob_out ] || fail "artifact was written despite the refusal"

expect_exit 0 "$CCCC" --test-run=basic -o basic_out oob.c
expect_exit 42 ./basic_out

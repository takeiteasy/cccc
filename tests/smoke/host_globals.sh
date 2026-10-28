# Under real SDK headers (--sysroot ... --use-system-headers) the program
# declares stdin/stdout/stderr, getopt's optarg/optind/opterr/optopt and
# environ as plain externs; they are the host's own objects in the VM.
# Skipped when no system headers are installed.
. "$(dirname "$0")/lib.sh"

if command -v xcrun >/dev/null 2>&1 && SDK=$(xcrun --show-sdk-path 2>/dev/null) &&
    [ -f "$SDK/usr/include/unistd.h" ]; then
    FLAGS="--sysroot $SDK --use-system-headers"
elif [ -f /usr/include/unistd.h ]; then
    FLAGS="--sysroot / --use-system-headers"
else
    echo "skip: no system headers"
    exit 0
fi

cat >streams.c <<'C'
#include <stdio.h>

int main(void) {
    fputs("to-stdout\n", stdout);
    fprintf(stderr, "to-stderr\n");
    return 42;
}
C

cat >getopt_env.c <<'C'
#include <getopt.h>
#include <string.h>
#include <unistd.h>

extern char **environ;
static int *where = &optind;

int main(void) {
    char *av[] = {"prog", "-a", "foo", "-b", 0};
    int   seen = 0, c;
#ifdef __APPLE__
    optreset = 1;
#endif
    optind = 1;
    opterr = 0;
    while ((c = getopt(4, av, "a:b")) != -1) {
        if (c == 'a' && !strcmp(optarg, "foo"))
            seen++;
        if (c == 'b')
            seen++;
    }
    if (seen != 2 || *where != 4 || opterr != 0)
        return 1;
    return environ && environ[0] ? 42 : 2;
}
C

cat >defines_main.c <<'C'
#include <getopt.h>

int main(void) {
    return optind == 77 ? 42 : 1;
}
C
cat >defines_other.c <<'C'
int optind = 77;
C

expect_exit 42 "$CCCC" $FLAGS streams.c
grep -q to-stdout out.txt || fail "stdout missing"
grep -q to-stderr err.txt || fail "stderr missing"
expect_exit 42 "$CCCC" $FLAGS getopt_env.c
expect_exit 42 "$CCCC" $FLAGS -3 getopt_env.c
expect_exit 42 "$CCCC" $FLAGS defines_main.c defines_other.c

expect_exit 0 "$CCCC" $FLAGS -c=native -o streams_native streams.c
expect_exit 42 ./streams_native
expect_exit 0 "$CCCC" $FLAGS -c=native -o getopt_native getopt_env.c
expect_exit 42 ./getopt_native

# The FFI flags apply to the load as to a call of the same name.
expect_fail "$CCCC" $FLAGS --disable-ffi streams.c

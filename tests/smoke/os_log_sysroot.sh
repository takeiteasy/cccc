# <os/log.h> from the macOS SDK: the include works in the VM, a call to os_log
# builds and runs under -c=native, and the VM rejects the call. Skipped without
# the macOS SDK.
. "$(dirname "$0")/lib.sh"

if ! command -v xcrun >/dev/null 2>&1 || ! SDK=$(xcrun --show-sdk-path 2>/dev/null) ||
    [ ! -f "$SDK/usr/include/os/log.h" ]; then
    echo "skip: no macOS SDK"
    exit 0
fi

cat >include.c <<'C'
#include <os/log.h>
int main(void) { return 42; }
C
cat >call.c <<'C'
#include <os/log.h>
int main(void) {
    os_log(OS_LOG_DEFAULT, "hello %d", 3);
    return 42;
}
C

expect_exit 42 "$CCCC" --sysroot "$SDK" include.c
expect_exit 0 "$CCCC" --sysroot "$SDK" -c=native -o call_native call.c
expect_exit 42 ./call_native
expect_fail "$CCCC" --sysroot "$SDK" call.c
grep -q "supported only when cccc emits C" err.txt || fail "VM error does not explain itself"

// --use-system-headers --no-builtin-includes: setjmp.h is compiler-owned and
// always resolves to CCCC's own jmp_buf/setjmp/longjmp, never the SDK's.
// The VM's jmp_buf layout is VM-specific (bytecode registers/PC), so a native
// setjmp.h would be silently wrong even if it happened to compile.
// CCCC_FLAGS: --use-system-headers --no-builtin-includes
#include <setjmp.h>

static jmp_buf env;

static void unwind(int n) {
    if (n == 3)
        longjmp(env, 42);
    unwind(n + 1);
}

int main(void) {
    int rv = setjmp(env);
    if (rv == 0) {
        unwind(0);
        return 1;
    }
    return rv;
}

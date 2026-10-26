// Header array typedefs (jmp_buf) are exempt from the checked-region array rule.

#include <setjmp.h>

[[cccc::checked]]
int f(void) {
    jmp_buf env;
    if (setjmp(env))
        return 42;
    longjmp(env, 1);
    return 0;
}

int main(void) {
    return f();
}

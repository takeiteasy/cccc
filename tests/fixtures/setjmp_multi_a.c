#include <setjmp.h>
int setjmp_multi_fn_a(void) {
    jmp_buf jb;
    if (setjmp(jb) == 0)
        longjmp(jb, 1);
    return 0;
}

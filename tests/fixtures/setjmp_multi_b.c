#include <setjmp.h>
int setjmp_multi_fn_b(void) {
    sigjmp_buf jb;
    if (sigsetjmp(jb, 1) == 0)
        siglongjmp(jb, 1);
    return 0;
}

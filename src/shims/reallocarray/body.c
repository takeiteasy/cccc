#include <errno.h>
static void *__cccc_reallocarray(void *__cccc_p, size_t __cccc_n, size_t __cccc_s) {
    if (__cccc_n && __cccc_s > (size_t)-1 / __cccc_n) {
        errno = ENOMEM;
        return 0;
    }
    return realloc(__cccc_p, __cccc_n * __cccc_s);
}

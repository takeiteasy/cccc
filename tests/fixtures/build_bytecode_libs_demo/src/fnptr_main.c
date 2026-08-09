// Host executable: call lib_add via function pointer (tests cross-module fn-ptr decay, #566).
#include "math_lib.h"

int main(void) {
    int (*fp)(int, int) = lib_add;
    return fp(40, 2); // 42 via function pointer
}

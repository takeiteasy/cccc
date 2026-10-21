// An attribute written after <stdio.h> is included survives under -c=native.
#include <stdio.h>
int ctor_ran;
int dtor_ran;
__attribute__((constructor)) void ctor(void) {
    ctor_ran = 1;
}
__attribute__((destructor)) void dtor(void) {
    dtor_ran = 1;
}
int main(void) {
    if (!ctor_ran)
        return 1;
    return 42;
}

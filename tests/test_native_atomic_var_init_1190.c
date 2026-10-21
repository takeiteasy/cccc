// CCCC_FLAGS: --std=c11
// ATOMIC_VAR_INIT round-trips under -c=native at --std=c11.
#include <stdatomic.h>
int main(void) {
    atomic_int x = ATOMIC_VAR_INIT(5);
    if (atomic_load(&x) != 5)
        return 1;
    return 42;
}

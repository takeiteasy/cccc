// A header-declared global round-trips under -c=native.
#include "fixtures/header_global_1047_smoke.h"
int main(void) {
    if (header_global_1047_smoke != 40)
        return 1;
    return header_global_1047_smoke + 2;
}

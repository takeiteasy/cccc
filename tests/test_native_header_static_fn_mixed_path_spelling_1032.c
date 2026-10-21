// CCCC_FLAGS: ./tests/fixtures/static_1032_a.c
// A header static function reached through differently spelled TU paths builds
// under -c=native.
#include "fixtures/shared_static_1032.h"
int use_a_1032(void);
int main(void) {
    return use_a_1032() + box_1032(22);
}

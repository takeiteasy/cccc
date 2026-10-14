extern int undefined_shared_callee(int);
#include "gnu_inline_shared.h"

int other_tu_value(void) {
    return shared_twice(20);
}

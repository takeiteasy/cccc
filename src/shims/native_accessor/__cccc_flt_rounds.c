#include <fenv.h>
static int __cccc_flt_rounds(void) {
    switch (fegetround()) {
    case FE_TOWARDZERO: return 0;
    case FE_TONEAREST:  return 1;
    case FE_UPWARD:     return 2;
    case FE_DOWNWARD:   return 3;
    default:            return -1;
    }
}

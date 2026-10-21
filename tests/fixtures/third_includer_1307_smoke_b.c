#include "config_1307_smoke.h"
int cfg_1307_smoke_helper(int x) {
    return x;
}
int third_includer_1307_smoke_call_a(void);
int main(void) {
    return third_includer_1307_smoke_call_a() + 34;
}

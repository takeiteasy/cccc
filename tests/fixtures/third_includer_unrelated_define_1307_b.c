// TU B for tests/test_serialize_third_includer_unrelated_define_1307.c
// (#1307). Plain second includer of plain_config_1307.h, no defines of
// its own -- present so the header is shared by more than one TU at all
// (mirrors #1305's own TU B).
#include "plain_config_1307.h"

int cfg_1307_helper(int x) {
    return x;
}

int third_includer_1307_call_a(void);

int main(void) {
    return third_includer_1307_call_a() + 34;
}

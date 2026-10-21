// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: long long ll_triple
// -m output keeps long long in the header prototype.
#include "fixtures/ll_1234_smoke.h"
long long ll_triple(long long v) {
    return v * 3;
}
int main(void) {
    return (int)ll_triple(14);
}

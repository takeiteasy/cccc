// A header prototype returning long long round-trips under -c=native.
#include "fixtures/ll_1234_smoke.h"
long long ll_triple(long long v) {
    return v * 3;
}
int main(void) {
    return (int)ll_triple(14);
}

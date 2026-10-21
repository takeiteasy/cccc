// A header included only for comptime is not replayed into -c=native output.
#include "fixtures/comptime_header_1048_smoke.h"
generate_result_1048();
int main(void) {
    return result_1048();
}

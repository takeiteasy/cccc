// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #651: CHKT3 is now emitted from codegen and populates
// AllocHeader.type_kind via an effective-type model (a store through a
// base pointer stamps the allocation's type; a load checks against it).
// Storing an int through p then reinterpreting the same base pointer as
// float* and loading through it must be caught as a type mismatch.
#include <stdlib.h>

int main(void) {
    int *p = malloc(sizeof(int));
    *p = 5;
    float *q = (float *)p; // same base address, reinterpreted
    return (int)*q;        // load as float: mismatches the stamped int type
}

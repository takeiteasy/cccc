// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __cccc_TaggedThing1286
// CCCC_EXPECT_STDOUT: static struct TaggedThing1286 __cccc_TaggedThing1286_0
// CCCC_HOST_CC: -Itests
// -m output hoists the compound literal into a static of the header type and
// the host compiler accepts it.
#include "fixtures/compound_literal_header_1286.h"
TaggedThing1286 *thing_1286 = &(TaggedThing1286){.a = 19, .b = 23};
int main(void) {
    return thing_1286->a + thing_1286->b;
}

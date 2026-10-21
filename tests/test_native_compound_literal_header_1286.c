// A compound literal of a header-declared type round-trips under -c=native.
#include "fixtures/compound_literal_header_1286.h"
TaggedThing1286 *thing_1286 = &(TaggedThing1286){.a = 19, .b = 23};
int main(void) {
    return thing_1286->a + thing_1286->b;
}

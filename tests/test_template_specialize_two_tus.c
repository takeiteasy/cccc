// CCCC_FLAGS: tests/fixtures/template_spec_tu_b.c
// CCCC_NATIVE_SKIP: multi-file -c=native emits a static template function instance twice
// TODO(#2005): drop the skip once the native serializer dedupes instances.
// A specialization in a header applies in every file that includes it.
#include "fixtures/template_spec.h"

int spec_tu_b_value(void);

int main(void) {
    return @inst(int) twice(10) + spec_tu_b_value() +
           @inst(long) twice(3L); // 30 + 6 + 6
}

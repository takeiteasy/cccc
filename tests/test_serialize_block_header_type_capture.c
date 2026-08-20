// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: #include <time\.h>[\s\S]*struct __cccc_block_env_
// CCCC_C4_SKIP
//
// #993: serialize_block_preamble() used to run ahead of the #include
// replay, so a by-value capture of a header-declared type (struct tm) was
// serialized while that type wasn't complete yet. The preamble now runs
// after the #include replay, so the header's own #include must appear in
// the output before the env struct that needs it -- a "contains both"
// check alone would pass on the old, broken ordering, so this asserts
// relative order via a single DOTALL regex instead.
//
// Note: this test only exercises -m (serializer shape), not a VM/native
// round trip -- the VM's own block-capture codegen truncates a by-value
// aggregate capture larger than 8 bytes to a single word (#994,
// pre-existing, unrelated to this fix), and struct tm is larger than 8
// bytes on every supported platform.

#include <time.h>

int main(void) {
    struct tm t;
    t.tm_year      = 42;
    int (^b)(void) = ^{
      return t.tm_year;
    };
    (void)b;
    return 42;
}

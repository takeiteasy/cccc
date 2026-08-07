// CCCC_FLAGS: -E --emit-cccc
// CCCC_EXPECT_STDOUT: cccc::emit
//
// --emit-cccc preserves CCCC-scoped attributes in -E output instead of
// stripping them (sibling to test_attr_target_strip_cccc.c, which pins the
// default stripping behaviour). [[cccc::test]]/comptime/etc. are consumed
// structurally at preprocess time regardless of this flag (they trigger
// real compiler behaviour, not just cosmetic attribute spelling), so
// [[cccc::emit]] -- a marker attribute output_attr() actually strips/emits
// -- is the attribute this flag can meaningfully preserve.
[[cccc::emit]] int u(void) { return 0; }
int main(void) { return 42; }

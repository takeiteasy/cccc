// CCCC_FLAGS: -m
// CCCC_C4_SKIP: -m dumps source and exits, no bytecode to round-trip
// CCCC_EXPECT_STDOUT: MyValue box\(MyValue t\)
// CCCC_REJECT_STDOUT: unsigned long box
//
// #999: a scalar typedef's parameter/return types used to always spell as
// their canonical underlying type (`unsigned long`) instead of the
// typedef name, dropping the alias entirely from serialized output. On
// most platforms this is only cosmetic (the canonical spelling and the
// typedef denote the same real type) -- but on a platform where they
// don't (e.g. `uint64_t` is `unsigned long long` on LP64 Darwin, not
// `unsigned long`), re-declaring a function this way collides with its
// real prototype in a header the output also includes ("conflicting
// types"), which is how this was found (dandy's DyValue/DyTag NaN-boxing
// accessors, ~takeiteasy/dandy). Verified `box`'s parameter/return
// printed as `unsigned long` before the fix.
typedef unsigned long MyValue;

MyValue box(MyValue t) { return t << 1; }

int main(void) { return (int)box(21); }

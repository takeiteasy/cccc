// EXPECT_COMPILE_ERROR
// #487 v1 boundary: a checked array must have a compile-time-constant
// extent -- a VLA extent has nothing for CB_COUNT to seed at declaration
// time, and this is rejected outright rather than silently left unchecked.
// Deliberately not passed --checked-pointers: this is a frontend rule that
// always applies, like the single-pointer-arithmetic ban.

int main(void) {
    int   n = 5;
    int a _Checked[n];
    return a[0];
}

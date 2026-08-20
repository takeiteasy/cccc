// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __attribute__\(\(vector_size\(16\)\)\)
// CCCC_REJECT_STDOUT: unknown type
//
// A GNU vector type must serialize as `<element>
// __attribute__((vector_size(N)))` with N the *total* byte size, not the lane
// count. Before this, TY_VECTOR had no case in serialize_type() and fell
// through to the `/* unknown type */` default arm, so -m/-c=native output for
// any program using a vector was not compilable C.
//
// The attribute spelling is what clang and gcc both accept in declaration
// position, verified against real clang before being chosen.

typedef int v4si __attribute__((vector_size(16)));

int main(void) {
    v4si a = {40, 2, 0, 0};
    v4si b = {2, 40, 0, 0};
    v4si c = a + b;
    return c[0];
}

// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct Point
// CCCC_REJECT_STDOUT: struct q
// -m output for a struct-by-value parameter names the real tag, never the
// parameter.
struct Point {
    int x;
    int y;
};
int helper(struct Point q) {
    return q.x + q.y;
}
int main(void) {
    struct Point z = {40, 2};
    return helper(z);
}

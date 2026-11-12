// CCCC_FLAGS: -E
// CCCC_EXPECT_STDOUT: __cccc_template\(Ts\.\.\.\) int sum\(Ts\.\.\. xs\)
// CCCC_EXPECT_STDOUT: \(xs \+ \.\.\. \+ 0\)
// CCCC_EXPECT_STDOUT: __cccc_template\(T, int\.\.\. Ns\) struct Nd
// CCCC_EXPECT_STDOUT: ,Ts\.\.\.
@template(Ts...) int sum(Ts... xs) {
    return (xs + ... + 0);
}
@template(T, int... Ns) struct Nd {
    T v[(Ns * ... * 1)];
};
@template(Ts...) int again(Ts... xs) {
    return sum@(Ts...)(xs...);
}
int main(void) {
    return 42;
}

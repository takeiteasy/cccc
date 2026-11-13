// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: comptime_template_error\.c:10:
// CCCC_REJECT_STDERR: comptime-include
// An error in a comptime-block template names the source file and line.
#pragma cccc comptime begin
@template(T) T bad(T v) {
    return v;
}
@template(T) T worse(T v) {
    return not_declared;
}
int use(void) {
    return worse @(int)(1);
}
#pragma cccc comptime end

int main(void) {
    return 0;
}

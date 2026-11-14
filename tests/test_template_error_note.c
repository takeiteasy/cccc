// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: in instantiation of template 'bad' \(bad_int\)
// CCCC_REJECT_STDERR: expanded from macro
@template(T) T bad(T a) { return a.nope; }

int main(void) {
    return @inst(int) bad(1);
}

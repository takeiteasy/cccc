// EXPECT_COMPILE_ERROR
// #487: `_Checked`/`_Nt_checked` are recognized positionally, immediately
// before `[` -- not followed by one, this is a clear diagnostic rather than
// falling through to a confusing generic parse error.

int main(void) {
    int a _Checked;
    return a;
}

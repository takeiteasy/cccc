// EXPECT_COMPILE_ERROR
// A bounds expression naming an out-of-scope identifier must fail to
// resolve (#770/#483): resolve_checked_bounds() re-parses the captured
// token span via assign(), which errors the normal way primary() errors on
// any undefined variable reference.

int main(void) {
    int * [[cccc::array, cccc::count(not_declared_anywhere)]] a = (int[3]){1, 2, 3};
    return a[0];
}

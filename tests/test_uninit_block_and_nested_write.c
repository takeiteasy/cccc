// CCCC_FLAGS: --safety=max
// CCCC_REJECT_STDOUT: UNINITIALIZED VARIABLE READ
// Two more shapes of the same #1008 class: a `__block` local written from
// inside a block literal, and a plain local written from inside a nested
// function -- both write through the static-link/block-box mechanism
// rather than a syntactic `var = expr;` at the variable's own declaring
// scope, so no explicit `&var` ever appears there either. addr_taken alone
// (set only for an explicit `&expr`) would miss these; the read-side CHKI
// guard also exempts is_captured (nested-function writes) and
// is_block_var (`__block` writes).
int main(void) {
    __block int x;
    void (^b)(void) = ^{
      x = 42;
    };
    b();

    int y;
    int inner(void) {
        y = 0;
        return 0;
    }
    inner();

    return x + y;
}

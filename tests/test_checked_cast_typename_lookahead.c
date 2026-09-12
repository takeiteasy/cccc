// CCCC_FLAGS: --checked-pointers
// #486: is_function() (src/parse_decl.c) speculatively re-parses every
// top-level function signature with in_type_lookahead=true before parsing
// it for real, to disambiguate a K&R-style function definition from an
// ordinary declaration -- and does so for every function definition in the
// file, not just K&R ones. cast()'s own checked-cast desugar and
// dynamic-source probe are guarded on !in_type_lookahead for exactly this
// reason (see the call site in cast(), src/parse_expr.c): a bounds cast
// used inside a function body sits well past the signature lookahead ever
// touches, but the guard must not accidentally suppress the desugar for the
// REAL parse that follows. This pins that a function using a bounds cast in
// its body compiles and runs normally.

int use(int *raw, int n) {
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n), cccc::assume]])raw;
    return p[0];
}

int main(void) {
    int arr[3] = {40, 1, 2};
    return use(arr, 3) + 2;
}

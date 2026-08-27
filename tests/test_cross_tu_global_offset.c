// CCCC_FLAGS: tests/fixtures/global_canon_957_defs.c
//
// #957: an extern global declared in one translation unit and defined in
// another (tests/fixtures/global_canon_957_defs.c, listed first via
// CCCC_FLAGS) must read the real defining Obj's data-segment slot, not
// whichever global happens to occupy the offset this TU's own
// declaration-only Obj was never assigned. See
// test_cross_tu_global_offset_reversed.c for the opposite file order.
extern int canon_g;

int main(void) {
    return canon_g;
}

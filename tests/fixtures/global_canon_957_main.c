// Fixture for tests/test_cross_tu_global_offset_reversed.c (#957): a main()
// that only *declares* canon_g, in a separate translation unit from
// tests/fixtures/global_canon_957_defs.c which defines it. Used with the
// defining fixture listed *after* this one on the command line, the
// opposite order from test_cross_tu_global_offset.c, to prove offset
// propagation is order-independent.
extern int canon_g;
int main(void) { return canon_g; }

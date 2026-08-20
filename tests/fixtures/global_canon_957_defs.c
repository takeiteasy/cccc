// Fixture for tests/test_cross_tu_global_offset.c and
// tests/test_cross_tu_global_offset_reversed.c (#957).
//
// canon_pad exists purely to occupy the data-segment slot canon_g would
// otherwise land in if a declaration-only Obj in another translation unit
// never had its offset propagated -- before the #957 fix, an extern
// reference to canon_g in another TU silently read canon_pad's value (7)
// instead of canon_g's (42).
int canon_g   = 42;
int canon_pad = 7;

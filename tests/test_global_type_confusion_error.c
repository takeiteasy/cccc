// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #752: CHKT3's byte-granular type shadow now also covers globals
// (vm->data_shadow, mirroring the heap shadow added by #653) -- a store
// through a global variable stamps its effective type in data_seg the same
// way a heap store does, so reinterpreting the global's address as a
// different type must be caught, not silently ignored the way it was when
// the shadow was heap-only.
int g;

int main(void) {
    g = 7;                        // stamps &g's effective type as int
    float *fp = (float *)&g;      // same address, reinterpreted
    return (int)*fp;              // load as float: mismatches the stamped int type
}

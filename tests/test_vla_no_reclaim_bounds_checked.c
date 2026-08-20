// CCCC_FLAGS: -2
// #981: heap reclamation must be fully disabled whenever any address-keyed
// safety feature is on (bounds checks, UAF/dangling detection, memory
// tagging, type checks, uninit detection, leak detection, heap canaries --
// see cc_heap_reclaim_flags_ok, cccc.h) since a reclaimed address getting
// reused would silently desync sorted_allocs/the effective-type shadow/
// etc from what's actually there. -2 enables CCCC_POINTER_SANITIZER
// (bounds/UAF/type checks), so a VLA declared repeatedly inside a loop
// must get a NEW, strictly increasing address every time -- never the
// same one twice. This is the only test in the suite that would catch the
// gate being absent or silently inverted; every other test here runs with
// the gate open.
int main(void) {
    int       k    = 4;
    long long prev = -1;
    for (int i = 0; i < 10; i++) {
        int u[k];
        u[0]           = i;
        long long addr = (long long)&u[0];
        if (addr <= prev)
            return 1;
        prev = addr;
    }
    return 42;
}

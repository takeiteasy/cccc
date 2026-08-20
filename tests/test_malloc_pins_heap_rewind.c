#include <stdlib.h>
// CCCC_FLAGS: -O0
// #981: a genuine user allocation (malloc & co, ALLOC_KIND_USER) sitting
// above a heap-reclamation watermark must pin the bump pointer at its own
// end -- heap_rewind_to (src/ops.c) must never sweep past it. Declares a
// VLA, then mallocs (landing above the VLA in the bump allocator), then
// lets the VLA's block exit, then does substantial further VLA churn (many
// more block-exit reclamations). If the malloc'd block were not pinned,
// heap_ptr would wrongly rewind below it and a later VLA allocation would
// land on top of (and corrupt) its bytes -- a bare write-then-immediate-
// read of the malloc'd block, with nothing reclaimed in between, would NOT
// catch that (the bytes are still physically there until something else
// overwrites them), so this deliberately reads m back only *after* the
// subsequent churn, once anything reusing its bytes would already have
// clobbered them.
static void churn_vlas(void) {
    // Deliberately much larger than u+m's combined footprint below, so
    // that if the pin were broken and heap_ptr wrongly rewound under m,
    // this VLA's own storage would stomp straight over m's data bytes
    // (not just its AllocHeader) regardless of exact struct-layout
    // arithmetic. Every element is written (not just the endpoints): at
    // the default safety level there is no memory poisoning, so a fresh
    // bump allocation's unwritten bytes are simply whatever was physically
    // there before -- a partial write could leave m's still-live values
    // in place by accident and mask the exact bug this test exists to
    // catch.
    int k = 256;
    for (int i = 0; i < 50; i++) {
        int v[k];
        for (int j = 0; j < k; j++)
            v[j] = -1;
        if (v[0] != -1 || v[k - 1] != -1)
            __builtin_trap();
    }
}

int main(void) {
    int  k = 4;
    int *m;
    {
        int u[k]; // triggers this block's HMRK/HREL
        u[0] = 1;
        m    = malloc(sizeof(int) * 4);
        if (!m)
            return 1;
        m[0] = 10;
        m[1] = 20;
        m[2] = 30;
        m[3] = 40;
    } // HREL here must stop at m's ALLOC_KIND_USER entry, not sweep past it

    for (int i = 0; i < 50; i++)
        churn_vlas();

    int sum = m[0] + m[1] + m[2] + m[3];
    free(m);
    return sum == 100 ? 42 : 1;
}

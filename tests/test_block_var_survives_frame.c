// CCCC_FLAGS: -O0
// #981: a __block variable's heap box (ALLOC_KIND_BLOCK_BOX) must never be
// swept by heap reclamation, at either block or frame exit -- Block_copy is
// expected to let it legitimately outlive its declaring frame. This is the
// load-bearing regression test for that exclusion: without it,
// heap_rewind_to (src/ops.c) could rewind the bump pointer straight through
// a still-referenced box the instant its declaring frame (or an enclosing
// VLA-declaring block) exits, and a later allocation reusing that address
// would silently corrupt it. Deliberately drives substantial VLA churn
// (many calls, each triggering both block- and frame-exit reclamation)
// *after* escaping the block, so any number of heap_rewind_to calls happen
// while the box is still the only thing anchoring that region.
typedef int (^IntBlock)(void);

static IntBlock make_capturing_block(void) {
    __block int captured = 99;
    int         k        = 4;
    int         u[k]; // VLA in the same frame -- this function's body triggers
                      // HMRK/HREL
    u[0]       = 1;
    IntBlock b = ^{
      return captured;
    };
    return Block_copy(b);
}

static void churn_vlas(void) {
    int k = 8;
    for (int i = 0; i < 50; i++) {
        int v[k];
        v[0] = i;
    }
}

int main(void) {
    IntBlock escaped = make_capturing_block();

    // Drive many more calls, each with their own VLA-in-loop reclamation
    // cycles, after the box has already escaped its declaring frame.
    for (int i = 0; i < 50; i++)
        churn_vlas();

    int result = escaped();
    Block_release(escaped);
    return result == 99 ? 42 : 1;
}

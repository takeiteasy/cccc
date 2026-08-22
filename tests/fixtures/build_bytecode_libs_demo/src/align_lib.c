// Bytecode library source for the #1136 module-adoption alignment
// regression: exercises cc_load_module's data_shift/tls_shift re-anchoring
// (src/bytecode.c) -- the lib's own gen() pass places these globals at
// their declared alignment relative to *its own* data_seg[0], and that
// alignment must survive the host re-anchoring the whole blob at
// data_shift when the host's own data segment isn't empty (main.c writes
// its own global first, so the lib doesn't just happen to land at offset 0
// again).
#include "align_lib.h"

char lib_pad;
_Alignas(32) int lib_align32;
char     lib_pad2[3];
__int128 lib_int128;

int lib_check_alignment(void) {
    if ((unsigned long long)&lib_align32 % 32 != 0)
        return 1;
    if ((unsigned long long)&lib_int128 % 16 != 0)
        return 2;
    return 0;
}

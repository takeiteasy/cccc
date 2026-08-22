// Host executable for the #1136 module-adoption alignment fixture. Writes
// its own global first so the linked module's data doesn't land at
// data_seg offset 0 (where every alignment "just works" trivially) --
// forces cc_load_module's data_shift re-anchoring to actually round.
#include "align_lib.h"

char host_pad[5]; // odd size: pushes data_ptr to a non-8-aligned offset

int main(void) {
    int rc = lib_check_alignment();
    return rc == 0 ? 42 : rc;
}

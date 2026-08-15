// #1014: a TU that only ever sees the incomplete forward-declared tag
// (never completes it) -- its Type stays incomplete, so
// same_type_or_origin() (deliberately, #892) matches it against *either*
// complete shape. This is the record find_tag_name()'s completeness-
// preferring lookup must still route to the header-exposed (plain-named)
// group, not whichever renamed group happens to be scanned first.
#include "dup_tag_1014.h"

int use_it_1014(void) {
    DyGC1014 *g = gc_open_1014();
    return gc_val_1014(g);
}

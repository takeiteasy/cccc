// #894 fixture: second link of the chain -- names a typedef declared in a
// DIFFERENT decl-only header, so resolving Outer894 must recursively splice
// Inner894 too.
#include "comptime_decl_index_transitive_a_894.h"
typedef Inner894 Mid894;
typedef Mid894 Outer894;

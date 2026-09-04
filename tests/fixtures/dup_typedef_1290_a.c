// #1290: includes the shared header and uses the type by value, so the
// header's own from_include TypeName record actually reaches
// ctx->defs/ctx->typedefs (matching src/serialize_type.c's own use of
// TypeVec by value).
#include "dup_typedef_1290.h"

int dup_typedef_1290_sum(DupTypedef1290 v) {
    return v.a + v.b;
}

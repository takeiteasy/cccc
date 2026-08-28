// -c=native regression (#1234): a header that spells a parameter or return
// type `long long` must not collide with -c=native's own re-emitted
// prototype/definition for the same function.
//
// CCCC's type system has no distinct `long long` kind -- `long` and
// `long long` share TY_LONG (identical representation on every LP64 target
// CCCC supports). The -c=native serializer used to spell every TY_LONG
// `long` unconditionally, so re-emitting `ll_triple` produced
// `long ll_triple(long)` next to the replayed `#include`'s
// `long long ll_triple(long long)` -- two non-identical declarations for one
// function, which the system C compiler rejects. A serialization-only
// spelling bit (Type.is_long_long) now preserves the original `long long`.
#include "native_long_long_1234.h"

long long ll_triple(long long v) {
    return v * 3;
}

int main(void) {
    long long r = ll_triple(14);
    return (int)r; // 42
}

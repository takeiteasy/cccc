// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: _Atomic _Bool f
// CCCC_REJECT_STDOUT: atomic_flag
//
// #1109: atomic_flag is the one bundled-header typedef whose host meaning
// diverges structurally -- C11 7.17 makes it a *struct* type (macOS SDK,
// glibc), while CCCC's own stdatomic.h spells an integer-flavoured
// `typedef _Atomic _Bool atomic_flag;`. Generated C re-includes
// <stdatomic.h>, so spelling the typedef name made every integer-style use
// fail to compile whenever the host resolved its own header instead of
// CCCC's (-I./include): "used type 'atomic_flag' (aka 'struct atomic_flag')
// where arithmetic or pointer type is required". The serializer now emits
// the canonical `_Atomic _Bool` for declarations and casts -- valid C11 on
// every host, header-independent -- and this test rejects ANY future output
// that reintroduces the divergent name.

#include <stdatomic.h>

int main(void) {
    atomic_flag f = ATOMIC_FLAG_INIT;
    if (__builtin_atomic_exchange(&f, 1))
        return 1;
    __builtin_atomic_store(&f, 0);
    return f ? 2 : 42;
}

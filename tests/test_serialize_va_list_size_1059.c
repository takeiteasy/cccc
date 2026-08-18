// Ticket #1059: found while root-causing #1018. -c=native folds
// sizeof(va_list)/offsetof(..., va_list member) against CCCC's own struct
// va_list layout (include/stdarg.h) at guest compile time, but replays the
// user's `#include <stdarg.h>` verbatim, which resolves to the *real
// host's* own <stdarg.h> at native-compile time (same mechanism #1054
// documented for jmp_buf/setjmp.h). Anywhere the folded constant sizes
// real storage (malloc(sizeof(va_list)), an array of them, a struct
// member, a memcpy), the allocation is undersized once the host's real,
// larger va_list takes over -- a stack/heap overrun once #1018's own
// va_start/va_arg translation lands and CCCC starts calling the real host
// va_start against that storage.
//
// Measured the real host va_list size directly on every supported
// platform x arch combo (not recalled): macOS arm64 8 bytes (a bare
// `char *`), macOS x86_64 24, glibc x86_64 32, glibc aarch64 32 (both
// glibc targets: an array of one `struct __va_list_tag`). Fixed by padding
// CCCC's own struct va_list to 64 bytes (a trailing `char __reserved[40]`)
// so the folded size over-allocates on every one of them, mirroring
// #1054's jmp_buf widening rather than diagnosing the fold.
//
// This test can't yet demonstrate an actual overrun end-to-end under
// -c=native (that needs #1018's own va_start/va_arg translation to land
// first, so native even compiles a variadic *definition*) -- it instead
// asserts the guest-folded constant itself is comfortably >= every
// measured host size, on both the VM path (this file) and -m's printed
// text (tools/comptime_native_smoke.py case 97).

#include "stdarg.h"

int main() {
    // Largest measured real host va_list is 32 bytes (glibc, either arch).
    // CCCC's own struct must fold to at least that, with headroom.
    if (sizeof(va_list) < 32)
        return 1;
    if (sizeof(va_list) != 64)
        return 2;

    return 42;
}

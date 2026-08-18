// Ticket #1054/#1030: under -c=native, setjmp()/longjmp() used to reach the
// output as ordinary function calls with the env argument cast to `long *`
// (the VM-side builtin parameter shape, parse_decl.c), relying on the
// program's own auto-captured `#include <setjmp.h>` to resolve to the real
// host header at native-compile time -- fragile, since a user -I path that
// happens to also contain CCCC's own bundled headers (e.g. this repo's own
// test harness's `-I./include`) shadows the real header with CCCC's
// declaration-free copy ("compiler builtin, no declaration needed" -- true
// for the VM, not for the real host functions -c=native calls), a hard
// "call to undeclared library function" from the host compiler. Separately,
// the real host jmp_buf is much larger than CCCC's own historical
// `long long[5]` (40 bytes; the real host writes/reads up to ~312 bytes,
// glibc aarch64's own figure, the largest of the four supported platform x
// arch combos -- see include/setjmp.h's own comment), so every native
// setjmp() call that *did* reach the real header silently overran env by
// ~150-270 bytes.
//
// #1030 filed the same defect from the --use-system-headers angle, with a
// since-stale error text ("implicit declaration of function 'longjmp'") --
// that doesn't reproduce; the real failure was warnings-only and exited 42
// by luck (env landed in unused bss). A file-scope static therefore can't
// serve as a regression test here.
//
// Fixed by (1) widening include/setjmp.h's jmp_buf to `long long[40]` (320
// bytes), comfortably covering every measured host (macOS arm64 192B, macOS
// x86_64 148B, glibc x86_64 200B, glibc aarch64 312B) -- guest-folded
// sizeof/offsetof over jmp_buf now already agrees with the real host write,
// since native storage stays CCCC's own type, never the host's jmp_buf
// alias (serialize.c never spells it that way, see
// typedef_alias_header_suppressed()); (2) never replaying the captured
// `#include <setjmp.h>` line into native/-m output at all, and instead
// always lowering setjmp/longjmp/_setjmp/_longjmp to calls to exactly
// `_setjmp`/`_longjmp` -- plain `extern`-declared functions on every
// supported host, unlike `setjmp` itself, a macro on glibc -- with an
// explicit declaration serialize_synth_setjmp_decls() emits on demand, and
// the env argument cast to `(void *)` instead of the implicit `(long *)`.
// This sidesteps the include-shadowing hazard entirely: the generated C no
// longer depends on any header resolving correctly for these four builtins.
//
// This test proves the overrun with a deterministic struct canary placed
// immediately after the jmp_buf (not an adjacent static/bss local, whose
// ordering isn't guaranteed) -- pre-fix, the guest folds the canary's
// offset to 40 (sizeof of the old 5-long jmp_buf) and the real host
// setjmp() overwrites it; post-fix the canary sits past the buffer and
// survives.

#include <setjmp.h>

struct canary_layout {
    jmp_buf env;
    unsigned long canary;
};

static struct canary_layout g;

static void unwind(void) {
    longjmp(g.env, 42);
}

int main(void) {
    g.canary = 0xC0FFEE1054UL;

    int rv = setjmp(g.env);
    if (rv == 0) {
        unwind();
        return 1; // unreachable
    }

    if (g.canary != 0xC0FFEE1054UL)
        return 2; // real host setjmp() overran the buffer

    return rv;
}

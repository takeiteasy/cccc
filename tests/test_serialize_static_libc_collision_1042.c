// Ticket #1042(c): tests/test_minilua.c defines `static int getmode(...)`
// (colliding, under -c=native, with macOS libc's real `mode_t
// getmode(const void *, mode_t)` from <unistd.h>) well BEFORE its own
// source's `#include <unistd.h>` -- legal C (a later, weaker declaration
// of an already-defined static function doesn't redefine it, confirmed
// directly: `clang -fsyntax-only` on the real source compiles clean). The
// bug is entirely a -c=native artifact: the #include-replay block
// (src/serialize.c) hoists every captured `#include` to the very top of
// the emitted C, unconditionally, ahead of every prototype/definition --
// inverting the source's own legal order and manufacturing a collision
// the user's program never actually has.
//
// Fixed with a host-symbol probe added to rename_colliding_static_names()
// (src/serialize.c, #1002): any static, defining Obj whose name resolves
// in the host libc's own symbol namespace (via dlsym on the SAME handle
// cc_load_libc()/find_libc() already use -- never RTLD_DEFAULT/
// dlopen(NULL), which would also see the compiler's OWN process and make
// output depend on which cccc binary happened to run it) gets the
// pass's existing "%s__cccc_dupN" rename. Renaming a static is always
// safe: it's file-local, and every reference goes through the same Obj,
// so correctness survives the rename regardless of what name it lands on
// -- this test checks the computed VALUE, not the emitted symbol name
// (tools/comptime_native_smoke.py's own case checks the -m text directly).
//
// `index` (a legacy BSD string function CCCC's own bundled string.h does
// NOT declare, unlike `getmode`/<unistd.h> which the real harness's
// `-I./include` never actually reaches -- see the ticket's own
// verification notes) is used here instead: it exists as a real exported
// symbol in every host libc this project supports (glibc, macOS
// libSystem), so the dlsym probe fires under the harness's default
// invocation with no extra flags needed.
//
// The probe itself is gated on the program actually replaying at least one
// real `#include` to the host compiler at all (any_real_include_replayed(),
// src/serialize.c) -- found necessary after a Linux/glibc 2.39 regression:
// a program with NO replayed include can never see this class of collision
// in the first place (nothing ever declares the name to the host), so the
// gate must stay off there or it churns unrelated output for no reason
// (test_serialize_polyfill_header_not_replayed.c hit exactly this, its own
// re-derived <stdbit.h> polyfill function colliding with a real, newer
// glibc symbol despite <stdbit.h>'s own #include never being replayed at
// all). This `#include <stdio.h>` is deliberately present, even though
// nothing in this file's own body needs it, purely to satisfy that gate --
// it's an ordinary, always-replayed header, matching the real minilua
// repro's own shape (a real #include genuinely reaches the host compiler).

#include <stdio.h>

static int index(int base, int step) {
    return base + step * 2;
}

int main(void) {
    int r = index(10, 16);
    if (r != 42)
        return 1;
    return 42;
}

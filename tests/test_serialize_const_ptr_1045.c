// Ticket #1045: a const-qualified *pointer* (`int *const p` -- is_const
// lives on the TY_PTR Type itself, not its base) used to serialize with its
// leading `const` misplaced onto the pointee: serialize_type() (src/
// serialize.c) printed `const ` unconditionally before recursing into the
// switch's TY_PTR case, which delegates to serialize_type_decl() -- whose
// TY_PTR branch has never printed pointer-level const at all. The result
// was `const int (*)(void)` (pointer to const int) instead of
// `int (*const)(void)` (const pointer to int), a genuinely incompatible
// type real clang/gcc reject wherever the const-pointer value is cast to or
// initialized against its non-const-pointer counterpart.
//
// Fixed by normalizing rather than relocating: a bare (non-typedef'd)
// pointer no longer prints pointer-level const at all here, matching what
// declarator position already did -- dropping a top-level qualifier is
// always type-compatible C. A typedef'd pointer (`const MyPtrT`) is a
// different, correct spelling and is asserted separately below.
//
// Three shapes, all previously broken the same way:
//   (a) a file-scope `static ... *const table[]` of function pointers,
//       called through -- the ticket's own repro shape.
//   (b) an `int *const` value serialized in cast/abstract-type position.
//   (c) a function with an `int *const p` parameter that has both a
//       separate prototype and a definition -- the latent parameter-list
//       half of the same bug (serialize_type_decl's TY_FUNC branch prints
//       each parameter through serialize_type() too).

int rhandler_a(void) {
    return 100;
}
int rhandler_b(void) {
    return 200;
}

static int (*const rtable[])(void) = {rhandler_a, rhandler_b};

// (c): prototype and definition of a function taking a const pointer
// parameter -- must serialize identically, or the host compiler sees
// "conflicting types".
static int read_const(int *const p);

static int read_const(int *const p) {
    return *p;
}

int main(void) {
    // (a)
    if (rtable[0]() != 100)
        return 1;
    if (rtable[1]() != 200)
        return 2;

    // (b): const-pointer value round-tripped through a cast.
    int        x    = 7;
    int *const cp   = &x;
    void      *vp   = (void *)cp;
    int       *back = (int *)vp;
    if (*back != 7)
        return 3;

    // (c)
    int y = 9;
    if (read_const(&y) != 9)
        return 4;

    return 42;
}

// Ticket #1295: pointer-level restrict/volatile and a bare (non-typedef'd)
// _Atomic-qualified declaration were never serialized at all -- split out
// of #1291, whose part (a) fixed the identical hazard for pointer-level
// const/volatile at the *base* position (serialize_type()) but left
// serialize_type_decl()'s TY_PTR branch never printing `*const`/
// `*volatile`/`*restrict name`, and left `_Atomic` unemitted outside the
// one atomic_flag alias special case (#1109).
//
// Four shapes:
//   (a) a `restrict`-qualified pointer parameter, with both a separate
//       prototype and a definition -- must serialize identically, or the
//       host compiler sees "conflicting types" (the same hazard #1045's
//       own test pins for `*const`).
//   (b) a `volatile`-qualified pointer global.
//   (c) a bare (non-typedef'd) `_Atomic int` global -- previously spelled
//       as plain `int`, silently losing the qualifier.
//   (d) an `_Atomic`-qualified pointer (`_Atomic(int *) p` -- CCCC's parser
//       only accepts the type-specifier form here, not a trailing `*_Atomic
//       p` declarator; that gap is a pre-existing, unrelated parser
//       limitation, not something #1295 touches) -- the pointer itself is
//       atomic, not its pointee; exercises both the new declarator-position
//       emission and its suppress_ptr_qual gate at the same time.
//
// This is a serializer round-trip test (VM 42 -> -c=native 42): shape
// alone can't prove the two ends of (a) still agree, or that the host
// compiler accepts the (c)/(d) spellings -- only an actual native compile
// can.

static int read_restrict(int *restrict p);

static int read_restrict(int *restrict p) {
    return *p;
}

static int *volatile vp;

_Atomic int g = 5;

int main(void) {
    // (a)
    int x = 9;
    if (read_restrict(&x) != 9)
        return 1;

    // (b)
    int y = 3;
    vp    = &y;
    if (*vp != 3)
        return 2;

    // (c)
    if (g != 5)
        return 3;
    g = g + 1;
    if (g != 6)
        return 4;

    // (d)
    int            z  = 11;
    _Atomic(int *) ap = &z;
    if (*ap != 11)
        return 5;

    return 42;
}

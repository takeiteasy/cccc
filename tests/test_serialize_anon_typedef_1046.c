// Ticket #1046: a typedef whose right-hand side is an anonymous struct/
// union/enum (`typedef struct { ... } P, *Pp;`) used to serialize
// `typedef P *Pp;` under -c=native referring to a `P` that was never
// printed at all, when `P` itself is never used by value anywhere in the
// program -- serialize_typedef_alias() (src/serialize.c) deliberately
// skips an anonymous aggregate's own combined `typedef struct {...} P;`
// line, on the assumption serialize_struct_def() already printed the body
// while walking the usage-collected ctx->defs -- which never contains a
// type nobody uses by value. Host compiler error: "unknown type name 'P'".
//
// The ticket's own title/body blamed segment_declarator_name()
// (src/macros.c) for missing brace-depth tracking -- that tracking already
// landed under #951 (src/macros.c:406-476) and isn't the defect here; this
// reproduces with no comptime/macro involvement at all. Root-caused and
// re-scoped during triage.
//
// emit_typedef_and_deps() now emits the aggregate body itself the first
// time it's reached, whether that's its own turn in the typedef loop or a
// dependency chase from another typedef (Pp -> P). A shared
// ctx->emitted_defs dedup set keeps this idempotent against the ordinary
// ctx->defs-driven path (a type CAN be both used by value AND spelled by a
// pointer typedef) and against a comptime re-parse's duplicate TypeName
// record for the same declaration -- which in turn needed
// same_type_or_origin() to gain a structural TY_ARRAY case (two
// independently-parsed occurrences of an array member, e.g. `char n[8]`,
// never shared pointer identity, so two structurally-identical duplicate
// bodies compared unequal and both got printed: "typedef redefinition with
// different types").

// Combined form, pointer alias only used indirectly, P never used by value.
typedef struct {
    int a[2];
} P, *Pp;

// Split form of the same shape.
typedef struct {
    long b;
} S;
typedef S *Sp;

// Tagged control: already worked before this fix (verified during triage),
// kept here so a future regression in the tagged path shows up too.
typedef struct Tag {
    int c;
} T, *Tp;

// Anonymous enum, same defect class as struct/union (#1046's fix extends to
// TY_ENUM in ensure_typedef_for_type_emitted()).
typedef enum { E_ONE = 1, E_TWO = 2 } E, *Ep;

// A member type reachable only via another struct's pointer member -- forces
// P's body through the dependency chase (ensure_typedef_for_type_emitted),
// not the top-level ctx->defs loop.
struct Holder {
    Pp p;
    Sp s;
    Tp t;
    Ep e;
};

int main(void) {
    struct Holder h;
    P             p = {{1, 2}};
    h.p             = &p;
    S s             = {3};
    h.s             = &s;
    T t             = {4};
    h.t             = &t;
    E e             = E_TWO;
    h.e             = &e;

    if (h.p->a[0] != 1 || h.p->a[1] != 2)
        return 1;
    if (h.s->b != 3)
        return 2;
    if (h.t->c != 4)
        return 3;
    if (*h.e != E_TWO)
        return 4;

    return 42;
}

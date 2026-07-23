// A global/static variable initialized from a compound literal used as the
// entire initializer, rather than a brace-list directly on the declaration.
// Previously this silently left the region zero-filled:
// gvar_initializer/write_gvar_data only knew how to serialize a populated
// Initializer children[] tree or a constant scalar expr, and fell through
// both for a struct/array/vector-typed init->expr referencing the compound
// literal's anonymous global. Fixed by (1) forcing a compound literal
// parsed while building a global/static initializer to resolve to an
// anonymous constant global (in_const_gvar_init, parse.c), even without its
// own storage-class specifier and even inside a function body, and (2)
// teaching write_gvar_data to splice bytes+relocations from a
// bare-reference-to-that-global init->expr instead of ignoring it.
//
// NOTE: this does NOT extend to copying an arbitrary global by value
// (`struct T x = y;`) -- real GCC/clang reject that too (see
// test_gvar_init_non_constant_error.c and its sibling for the vector case);
// only a compound literal's own synthesized global is treated as constant
// (Obj.is_compound_literal gates the splice-in path).

struct P {
    int x;
    int y;
};

typedef float v4sf __attribute__((vector_size(16)));

// File-scope: compound literal as the entire initializer.
struct P g1 = (struct P){5, 6};

// File-scope: vector via compound literal.
v4sf gv = (v4sf){1.0f, 2.0f, 3.0f, 4.0f};

// Array mixing a compound-literal element with a braced element.
struct P garr[2] = {(struct P){7, 8}, {9, 10}};

// An address-of-another-global inside a compound literal is itself a legal
// static initializer (unlike copying a global's *value*, see
// test_gvar_init_from_global_error.c) -- exercises write_gvar_data's
// relocation-splice loop (offset + r->offset), not just raw bytes.
int reloc_a = 10, reloc_b = 20;
struct Ptr {
    int v;
    int *p;
};
struct Ptr reloc_arr[2] = {(struct Ptr){1, &reloc_a}, (struct Ptr){2, &reloc_b}};

int main(void) {
    if (g1.x != 5) return 1;
    if (g1.y != 6) return 2;

    if (gv[0] != 1.0f) return 3;
    if (gv[3] != 4.0f) return 4;

    if (garr[0].x != 7) return 5;
    if (garr[0].y != 8) return 6;
    if (garr[1].x != 9) return 7;
    if (garr[1].y != 10) return 8;

    if (*reloc_arr[0].p != 10) return 9;
    if (*reloc_arr[1].p != 20) return 10;

    // Local `static` variable, still initialized from a compound literal
    // that has no storage-class specifier of its own (relies on
    // in_const_gvar_init forcing the anonymous-global path).
    static struct P b = (struct P){3, 4};
    if (b.x != 3) return 11;
    if (b.y != 4) return 12;

    return 42;
}

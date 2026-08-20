// CCCC_FLAGS: --testing
// Consolidated suite: extern global redeclaration canonicalization (#957)
// Source tests: test_global_canon_extern_then_define,
// test_global_canon_define_then_extern,
//   test_global_canon_tentative_then_init,
//   test_global_canon_init_then_tentative, test_global_canon_static_redecl,
//   test_global_canon_incomplete_array, test_global_canon_address_relocation,
//   test_global_canon_sizeof_never_defined

// Every declaration of a same-named global variable within one translation
// unit must canonicalize onto a single Obj (src/parse.c's global_variable(),
// merge_global_decl()) so all references share one data-segment offset
// (gen_addr bakes the offset in as an immediate -- see src/codegen.c).
// Before #957 each redeclaration created its own Obj and only the first one
// ever got initialized/used by codegen; a function compiled against an
// earlier `extern` declaration read whichever value that Obj's slot
// happened to hold (typically 0), regardless of a later definition.

extern int canon_a;
int use_canon_a_before_define(void) {
    return canon_a;
}
int        canon_a = 42;

int        canon_b = 42;
extern int canon_b;

int        canon_t1;
int        canon_t1 = 5;

int        canon_t2 = 6;
int        canon_t2;

static int canon_s1;
static int canon_s1 = 3;

extern int canon_arr[];
int        canon_arr[3] = {1, 2, 3};

int        canon_q      = 7;
extern int canon_q;
int       *canon_pq = &canon_q;

extern int canon_never_defined;

#pragma cccc suite begin "global_canonicalization"

// test_global_canon_extern_then_define
[[cccc::test(return = 42)]]
int test_global_canon_extern_then_define(void) {
    return use_canon_a_before_define();
}

// test_global_canon_define_then_extern
[[cccc::test(return = 42)]]
int test_global_canon_define_then_extern(void) {
    return canon_b;
}

// test_global_canon_tentative_then_init
[[cccc::test(return = 42)]]
int test_global_canon_tentative_then_init(void) {
    return canon_t1 == 5 ? 42 : 1;
}

// test_global_canon_init_then_tentative
[[cccc::test(return = 42)]]
int test_global_canon_init_then_tentative(void) {
    // A later tentative redeclaration (`int canon_t2;`) must not clobber the
    // earlier initializer.
    return canon_t2 == 6 ? 42 : 1;
}

// test_global_canon_static_redecl
[[cccc::test(return = 42)]]
int test_global_canon_static_redecl(void) {
    return canon_s1 == 3 ? 42 : 1;
}

// test_global_canon_incomplete_array
[[cccc::test(return = 42)]]
int test_global_canon_incomplete_array(void) {
    // The incomplete `extern int canon_arr[];` declaration must adopt the
    // later complete-array definition's size, since the data-segment
    // allocation loop sizes the slot from var->ty->size.
    if (sizeof(canon_arr) != 3 * sizeof(int))
        return 1;
    return canon_arr[0] + canon_arr[1] + canon_arr[2] == 6 ? 42 : 2;
}

// test_global_canon_address_relocation
[[cccc::test(return = 42)]]
int test_global_canon_address_relocation(void) {
    // A file-scope initializer taking &canon_q (apply_global_relocations)
    // must resolve to the same canonical Obj as every other reference.
    return *canon_pq == 7 ? 42 : 1;
}

// test_global_canon_sizeof_never_defined
[[cccc::test(return = 42)]]
int test_global_canon_sizeof_never_defined(void) {
    // sizeof() never reaches gen_addr, so a declaration-only extern global
    // that is only ever sizeof()'d must compile even though it is never
    // defined -- this is what forbids marking a global "referenced" at
    // parse time instead of in gen_addr. See test_extern_global_undefined.c
    // for the case where it *is* referenced via an actual load.
    return sizeof(canon_never_defined) == sizeof(int) ? 42 : 1;
}

#pragma cccc suite end

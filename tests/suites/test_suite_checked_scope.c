// CCCC_FLAGS: --testing
// Checked regions (#485): [[cccc::checked]]/[[cccc::unchecked]] on a function
// definition or compound statement, and #pragma cccc checked/unchecked
// begin/end. These are pure parse/type-check diagnostics -- always on,
// independent of --checked-pointers (deliberately not passed here) -- see
// docs/safety.md's "Checked Regions" section. Positive cases live here
// (single compiling file); compile-error cases cannot share a compiling
// file with these (--testing=native doesn't support in-suite negative
// tests, #1033 v1) and live in standalone tests/test_checked_scope_*.c
// files instead, mirroring test_suite_checked_pointers.c's own split.

#include <stdio.h>

// ---------------------------------------------------------------------
// Basic function/block attribute forms, and nesting.
// ---------------------------------------------------------------------

[[cccc::checked]]
int checked_array_sum(void) {
    int n                                  = 4;
    int *[[cccc::array, cccc::count(n)]] a = (int[4]){1, 2, 3, 4};
    return a[0] + a[1] + a[2] + a[3];
}

[[cccc::test]]
void test_checked_function_body(void) {
    AssertEq(checked_array_sum(), 10);
}

[[cccc::checked]]
int checked_with_unchecked_escape(void) {
    int n                                  = 1;
    int *[[cccc::array, cccc::count(n)]] a = (int[1]){41};
    int result;
    [[cccc::unchecked]] {
        int *raw = (int *)a; // fine -- inside the escape hatch
        result   = raw[0] + 1;
    }
    return result;
}

[[cccc::test]]
void test_unchecked_block_nested_in_checked_function(void) {
    AssertEq(checked_with_unchecked_escape(), 42);
}

[[cccc::checked]]
int three_deep_nesting(void) {
    int total = 0;
    [[cccc::unchecked]] {
        int *p = &total;                 // fine -- unchecked block
        [[cccc::checked]] {
            int *[[cccc::single]] q =
                (int *[[cccc::single, cccc::assume]])p; // fine -- assume
            *q = 40;
        }
        *p += 2;
    }
    return total;
}

[[cccc::test]]
void test_three_deep_alternating_nesting(void) {
    AssertEq(three_deep_nesting(), 42);
}

// ---------------------------------------------------------------------
// What is explicitly NOT banned inside a checked region.
// ---------------------------------------------------------------------

#include <stddef.h>

int *g_raw;
int  g_one = 7;

[[cccc::checked]]
int null_constants_allowed(void) {
    int *[[cccc::single]] a                = NULL;
    int *[[cccc::single]] b                = (int *[[cccc::single]])0;
    int *[[cccc::array, cccc::count(0)]] c = nullptr;
    return a == NULL && b == NULL && c == NULL ? 42 : 0;
}

[[cccc::test]]
void test_null_constants_allowed_in_region(void) {
    AssertEq(null_constants_allowed(), 42);
}

[[cccc::checked]]
int visible_objects_convert_implicitly(void) {
    int arr[4]                                = {1, 2, 3, 4};
    int *[[cccc::array, cccc::count(4)]] a    = arr;
    int *[[cccc::array, cccc::count(3)]] b    = arr + 1;
    int *[[cccc::array, cccc::count(2)]] c    = (int[2]){5, 6};
    int *[[cccc::single]] d                   = &g_one;
    char *[[cccc::ntarray, cccc::count(2)]] s = "hi";
    int *[[cccc::array, cccc::count(3)]] e    = a + 1;
    return a[0] + b[0] + c[0] + *d + s[0] - 'h' + e[0];
}

[[cccc::test]]
void test_visible_objects_convert_in_region(void) {
    AssertEq(visible_objects_convert_implicitly(), 1 + 2 + 5 + 7 + 0 + 2);
}

[[cccc::checked]]
int take_array(int *[[cccc::array, cccc::count(n)]] p, int n) {
    return p[n - 1];
}

[[cccc::checked]] int *[[cccc::single]] give_single(int *[[cccc::single]] p) {
    return p;
}

[[cccc::test]]
void test_checked_args_and_returns_in_region(void) {
    int arr[3] = {1, 2, 3};
    g_raw      = arr;
    AssertEq(take_array(arr, 3), 3);
    AssertEq(*give_single(&g_one), 7);
}

[[cccc::checked]]
int bounds_casts_allowed(int n) {
    int *[[cccc::array, cccc::count(n)]] a =
        (int *[[cccc::array, cccc::count(n), cccc::assume]])g_raw;
    return a[0];
}

[[cccc::test]]
void test_assume_cast_allowed_in_region(void) {
    int arr[3] = {42, 2, 3};
    g_raw      = arr;
    AssertEq(bounds_casts_allowed(3), 42);
}

int *[[cccc::single]] checked_return_type(int *[[cccc::single]] p);

[[cccc::checked]]
int checked_pointer_array(void) {
    int *[[cccc::single]] slots[2] = {&g_one, NULL};
    return *slots[0] + (slots[1] == NULL) + *checked_return_type(&g_one);
}

[[cccc::checked]] int *[[cccc::single]] checked_return_type(
    int *[[cccc::single]] p) {
    return p;
}

[[cccc::test]]
void test_checked_arrays_and_return_types_allowed(void) {
    AssertEq(checked_pointer_array(), 7 + 1 + 7);
}

[[cccc::checked]]
int auto_locals_allowed(int *[[cccc::single]] s) {
    auto n = 40;
    auto c = s;
    [[cccc::unchecked]] {
        auto raw = g_raw;
        (void)raw;
    }
    return n + *c;
}

[[cccc::test]]
void test_auto_locals_in_region(void) {
    int two = 2;
    AssertEq(auto_locals_allowed(&two), 42);
}

[[cccc::checked]]
int bounds_unknown_from_raw(void) {
    int *[[cccc::array, cccc::bounds(unknown)]] a = g_raw;
    int *[[cccc::array, cccc::bounds(unknown)]] b =
        (int *[[cccc::array, cccc::bounds(unknown)]])g_raw;
    return a[0] + b[0];
}

[[cccc::test]]
void test_bounds_unknown_from_raw_allowed(void) {
    int arr[2] = {21, 2};
    g_raw      = arr;
    AssertEq(bounds_unknown_from_raw(), 42);
}

[[cccc::checked]]
int bounds_unknown_escape_hatch(void) {
    int arr[3]                                    = {10, 20, 12};
    int *[[cccc::array, cccc::bounds(unknown)]] a = arr;
    return a[0] + a[1] + a[2];
}

[[cccc::test]]
void test_bounds_unknown_allowed(void) {
    AssertEq(bounds_unknown_escape_hatch(), 42);
}

[[cccc::checked]]
int checked_calls_libc(void) {
    // Calling into an unchecked-prototype libc function is allowed --
    // interop is a documented v1 gap, not enforced. puts() takes a
    // `const char *`, an unchecked pointer parameter on puts()'s own
    // (header, bodyless) prototype -- but that prototype's parameter never
    // reaches create_param_lvars() (no body), and the cast/decl ban is only
    // about what THIS file declares, not what it calls.
    return puts("ok");
}

[[cccc::test]]
void test_libc_call_from_checked_region_allowed(void) {
    Assert(checked_calls_libc() >= 0);
}

// ---------------------------------------------------------------------
// Region attribute spellings.
// ---------------------------------------------------------------------

__attribute__((checked)) int gnu_spelling_checked(void) {
    int *[[cccc::single]] p = 0;
    (void)p;
    return 42;
}

[[cccc::test]]
void test_gnu_attribute_spelling(void) {
    AssertEq(gnu_spelling_checked(), 42);
}

@checked int at_prefix_spelling(void) {
    int *[[cccc::single]] p = 0;
    (void)p;
    return 42;
}

[[cccc::test]]
void test_at_prefix_spelling(void) {
    AssertEq(at_prefix_spelling(), 42);
}

// ---------------------------------------------------------------------
// State does not leak: after a checked function/block ends, ordinary
// unchecked code right after is unaffected.
// ---------------------------------------------------------------------

[[cccc::checked]]
void a_checked_function(void) {
    int *[[cccc::single]] p = 0;
    (void)p;
}

int an_unchecked_function(void) {
    int *p = 0; // must NOT be rejected -- no region here
    (void)p;
    return 42;
}

[[cccc::test]]
void test_region_does_not_leak_across_functions(void) {
    a_checked_function();
    AssertEq(an_unchecked_function(), 42);
}

int unchecked_after_block(void) {
    [[cccc::checked]] {
        int *[[cccc::single]] q = 0;
        (void)q;
    }
    int *p = 0; // must NOT be rejected -- block region already closed
    (void)p;
    return 42;
}

[[cccc::test]]
void test_region_does_not_leak_across_block(void) {
    AssertEq(unchecked_after_block(), 42);
}

// ---------------------------------------------------------------------
// #1331: Checked C keyword spellings `_Checked { ... }` / `_Unchecked
// { ... }` -- a second spelling for the same region state, recognized
// positionally (a name immediately followed by '{') rather than as a
// reserved keyword.
// ---------------------------------------------------------------------

int underscore_checked_array_sum(void) {
    int n = 4;
    _Checked {
        int *[[cccc::array, cccc::count(n)]] a = (int[4]){1, 2, 3, 4};
        return a[0] + a[1] + a[2] + a[3];
    }
}

[[cccc::test]]
void test_underscore_checked_block(void) {
    AssertEq(underscore_checked_array_sum(), 10);
}

[[cccc::checked]]
int underscore_unchecked_escape_in_attr_function(void) {
    int n                                  = 1;
    int *[[cccc::array, cccc::count(n)]] a = (int[1]){41};
    int result;
    _Unchecked {
        int *raw = (int *)a; // fine -- inside the escape hatch
        result   = raw[0] + 1;
    }
    return result;
}

[[cccc::test]]
void test_underscore_unchecked_escape_in_attr_function(void) {
    AssertEq(underscore_unchecked_escape_in_attr_function(), 42);
}

int underscore_three_deep_alternating_nesting(void) {
    int total = 0;
    _Checked {
        _Unchecked {
            int *p = &total; // fine -- unchecked block
            _Checked {
                int *[[cccc::single]] q =
                    (int *[[cccc::single, cccc::assume]])p; // fine -- assume
                *q = 40;
            }
            *p += 2;
        }
    }
    return total;
}

[[cccc::test]]
void test_underscore_three_deep_alternating_nesting(void) {
    AssertEq(underscore_three_deep_alternating_nesting(), 42);
}

// Mixed spellings: the attribute form and the keyword form nest freely
// against each other -- innermost always wins regardless of which spelling
// it used to get there.
[[cccc::checked]]
int mixed_spelling_attr_then_keyword(void) {
    int n                                  = 1;
    int *[[cccc::array, cccc::count(n)]] a = (int[1]){20};
    int result;
    _Unchecked {
        int *raw = (int *)a;
        result   = raw[0];
    }
    return result;
}

[[cccc::test]]
void test_mixed_spelling_attr_then_keyword(void) {
    AssertEq(mixed_spelling_attr_then_keyword(), 20);
}

int mixed_spelling_keyword_then_attr(void) {
    int result = 0;
    _Checked {
        [[cccc::unchecked]] {
            int *p = &result; // fine -- unchecked escape from a keyword region
            *p     = 22;
        }
    }
    return result;
}

[[cccc::test]]
void test_mixed_spelling_keyword_then_attr(void) {
    AssertEq(mixed_spelling_keyword_then_attr(), 22);
}

#pragma cccc unchecked begin
// A keyword-form region overrides the surrounding pragma-introduced state
// for its own lexical extent, the same as the attribute form already does.
int underscore_checked_overrides_pragma(void) {
    int n = 1;
    _Checked {
        int *[[cccc::array, cccc::count(n)]] a = (int[1]){42};
        return a[0];
    }
}
#pragma cccc unchecked end

[[cccc::test]]
void test_underscore_checked_overrides_pragma(void) {
    AssertEq(underscore_checked_overrides_pragma(), 42);
}

// A plain identifier spelled `_Checked`/`_Unchecked` is never reclassified --
// the recognition is purely positional (name immediately followed by '{').
int _Checked_like_name_is_unaffected(void) {
    int _Checked   = 20;
    int _Unchecked = 22;
    return _Checked + _Unchecked;
}

[[cccc::test]]
void test_underscore_name_not_reclassified(void) {
    AssertEq(_Checked_like_name_is_unaffected(), 42);
}

// ---------------------------------------------------------------------
// [[cccc::trusted]] prototypes are callable from a region.
// ---------------------------------------------------------------------

[[cccc::trusted]] int trusted_first(int *p);

int trusted_first(int *p) {
    return p[0];
}

[[cccc::checked]]
int call_trusted(void) {
    int *[[cccc::array, cccc::count(1)]] a = (int[1]){7};
    return trusted_first(a);
}

[[cccc::test]]
void test_trusted_prototype(void) {
    AssertEq(call_trusted(), 7);
}

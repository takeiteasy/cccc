// CCCC_FLAGS: --testing --checked-pointers
// Suite: the Checked C spellings _Ptr<T>, _Array_ptr<T> and _Nt_array_ptr<T>,
// with a trailing `: count(n)` bounds declaration, behave like the attributes.

#include <stdlib.h>

typedef _Array_ptr<int> IntArr;

struct Buf {
    _Array_ptr<int> data : count(len);
    int             len;
    unsigned        flags : 3;
};

static _Array_ptr<int> g_arr : count(4) = (int[4]){10, 20, 30, 40};

static _Array_ptr<int> head(_Array_ptr<int> a : count(n), int n) : count(n) {
    return a;
}

static int sum(_Array_ptr<int> a : count(n), int n) {
    int total = 0;
    for (int i = 0; i < n; i++)
        total += a[i];
    return total;
}

[[cccc::test]]
void same_type_as_the_attribute_form(void) {
    _Ptr<int>           one = 0;
    _Array_ptr<int>     arr = 0;
    _Nt_array_ptr<char> str = 0;
    AssertEq(_Generic(one, int *[[cccc::single]]: 1, default: 0), 1);
    AssertEq(_Generic(arr, int *[[cccc::array]]: 1, default: 0), 1);
    AssertEq(_Generic(str, char *[[cccc::ntarray]]: 1, default: 0), 1);
}

[[cccc::test]]
void single_pointer(void) {
    int       x = 7;
    _Ptr<int> p = &x;
    *p          = 9;
    AssertEq(x, 9);
    AssertEq((int)sizeof(_Ptr<int>), (int)sizeof(int *));
}

[[cccc::test]]
void local_count_in_bounds(void) {
    int             n            = 5;
    _Array_ptr<int> a : count(n) = (int[5]){1, 2, 3, 4, 5};
    AssertEq(a[0] + a[4], 6);
}

[[cccc::test(exit_code = 255)]]
void local_count_out_of_bounds_traps(void) {
    int             n            = 5;
    _Array_ptr<int> a : count(n) = (int[5]){1, 2, 3, 4, 5};
    volatile int    i            = 5;
    int             x            = a[i];
    (void)x;
}

[[cccc::test]]
void global_count(void) {
    AssertEq(g_arr[3], 40);
}

[[cccc::test(exit_code = 255)]]
void global_count_out_of_bounds_traps(void) {
    volatile int i = 4;
    int          x = g_arr[i];
    (void)x;
}

[[cccc::test]]
void parameter_and_return_bounds(void) {
    int arr[3] = {1, 2, 3};
    AssertEq(sum(arr, 3), 6);
    AssertEq(head(arr, 3)[2], 3);
}

[[cccc::test(exit_code = 255)]]
void return_bounds_trap(void) {
    int          arr[3] = {1, 2, 3};
    volatile int i      = 3;
    int          x      = head(arr, 3)[i];
    (void)x;
}

[[cccc::test]]
void struct_member_beside_a_bit_field(void) {
    struct Buf b = {(int[2]){4, 5}, 2, 5};
    AssertEq(b.data[1], 5);
    AssertEq((int)b.flags, 5);
}

[[cccc::test(exit_code = 255)]]
void struct_member_out_of_bounds_traps(void) {
    struct Buf   b = {(int[2]){4, 5}, 2, 0};
    volatile int i = 2;
    int          x = b.data[i];
    (void)x;
}

[[cccc::test]]
void typedef_declarators_keep_their_own_bounds(void) {
    int    store[3]     = {1, 2, 3};
    IntArr a : count(2) = store, b : count(3) = store;
    AssertEq(b[2], 3);
    AssertEq(a[1], 2);
}

[[cccc::test(exit_code = 255)]]
void typedef_declarator_bound_traps(void) {
    int          store[3]     = {1, 2, 3};
    IntArr       a : count(2) = store, b : count(3) = store;
    volatile int i = 2;
    int          x = a[i];
    (void)x;
}

[[cccc::test]]
void byte_count_and_bounds_forms(void) {
    int             store[4]                         = {1, 2, 3, 4};
    _Array_ptr<int> bytes : byte_count(16)           = store;
    _Array_ptr<int> range : bounds(store, store + 4) = store;
    AssertEq(bytes[3] + range[0], 5);
}

[[cccc::test]]
void nt_array_pointer(void) {
    _Nt_array_ptr<char> s : count(3) = "abc";
    AssertEq(s[2], 'c');
}

[[cccc::test]]
void nested_and_function_pointees(void) {
    int                   x            = 1;
    _Ptr<int>             p            = &x;
    _Ptr<_Ptr<int>>       pp           = &p;
    _Array_ptr<_Ptr<int>> a : count(1) = pp;
    AssertEq(**pp, 1);
    AssertEq(*a[0], 1);
    _Ptr<int(int)> f = 0;
    AssertEq(f == 0, 1);
}

[[cccc::test]]
void qualifiers_and_casts(void) {
    int             x = 3;
    const _Ptr<int> p = &x;
    AssertEq(*(_Ptr<int>)p, 3);
}

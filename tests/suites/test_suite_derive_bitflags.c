// CCCC_FLAGS: --testing
// Suite: @bitflags, run on the VM and under -c=native.

#include <cccc/derive.h>
#include <string.h>

@bitflags typedef enum { NONE = 0, R = 1, W = 2, X = 4, BIG = 1 << 20 } Perm;

@bitflags("app") enum Mode { M_A = 1, M_B = 2, M_ALIAS = 2 };

@bitflags enum Plain { P_X = 1 };

@bitflags typedef enum {
    F_RW  = 3,
    F_R   = 1,
    F_W   = 2,
    F_X   = 4,
    F_RWX = 7,
} Fl;

@bitflags typedef enum { O_A = 1, O_B = 2, O_C = 4, O_AB = 3, O_BC = 6 } Ov;

[[cccc::test]] void test_set_clear_toggle_has(void) {
    Perm p = Perm_set(NONE, R);
    p      = Perm_set(p, X);
    AssertEq((int)p, R | X);
    AssertEq(!!Perm_has(p, R), 1);
    AssertEq(!!Perm_has(p, W), 0);
    AssertEq(!!Perm_has(p, R | X), 1);
    AssertEq(!!Perm_has(p, R | W), 0);
    AssertEq(!!Perm_has(p, NONE), 1);
    p = Perm_clear(p, R);
    AssertEq((int)p, X);
    p = Perm_toggle(p, W);
    AssertEq((int)p, W | X);
    p = Perm_toggle(p, W);
    AssertEq((int)p, X);
    AssertEq((int)Perm_set(NONE, BIG), BIG);
}

[[cccc::test]] void test_format(void) {
    char buf[64];
    AssertEq((int)Perm_format(R | X, buf, sizeof buf), 3);
    AssertEq(strcmp(buf, "R|X"), 0);
    Perm_format(R | W | X | BIG, buf, sizeof buf);
    AssertEq(strcmp(buf, "R|W|X|BIG"), 0);
    Perm_format(NONE, buf, sizeof buf);
    AssertEq(strcmp(buf, "NONE"), 0);
    Perm_format((Perm)(R | 0x40), buf, sizeof buf);
    AssertEq(strcmp(buf, "R|0x40"), 0);
    Perm_format((Perm)0x1000, buf, sizeof buf);
    AssertEq(strcmp(buf, "0x1000"), 0);
}

[[cccc::test]] void test_format_truncates_like_snprintf(void) {
    char tiny[4];
    AssertEq((int)Perm_format(R | W | X, tiny, sizeof tiny), 5);
    AssertEq(strcmp(tiny, "R|W"), 0);
    AssertEq((int)Perm_format(R | W | X, NULL, 0), 5);
    char one[1] = {'z'};
    AssertEq((int)Perm_format(R, one, 1), 1);
    AssertEq((int)one[0], 0);
}

[[cccc::test]] void test_prefix_and_duplicate_values(void) {
    enum Mode m = app_Mode_set((enum Mode)0, M_B);
    AssertEq((int)m, M_B);
    char buf[16];
    app_Mode_format(M_A | M_B, buf, sizeof buf);
    AssertEq(strcmp(buf, "M_A|M_B"), 0);
    AssertEq(!!app_Mode_has(m, M_ALIAS), 1);
}

[[cccc::test]] void test_no_zero_constant(void) {
    char buf[16];
    Plain_format((enum Plain)0, buf, sizeof buf);
    AssertEq(strcmp(buf, "0"), 0);
}

[[cccc::test]] void test_composite_alias(void) {
    char buf[64];
    Fl_format((Fl)7, buf, sizeof buf);
    AssertEq(strcmp(buf, "F_RWX"), 0);
    Fl_format((Fl)(F_R | F_W), buf, sizeof buf);
    AssertEq(strcmp(buf, "F_RW"), 0);
    Fl_format((Fl)(F_R | F_X), buf, sizeof buf);
    AssertEq(strcmp(buf, "F_R|F_X"), 0);
    Fl_format((Fl)(F_R | F_W | 0x40), buf, sizeof buf);
    AssertEq(strcmp(buf, "F_RW|0x40"), 0);
    AssertEq(!!Fl_has((Fl)7, F_RW), 1);
    AssertEq(!!Fl_has((Fl)F_R, F_RW), 0);
    AssertEq((int)Fl_set((Fl)0, F_RW), 3);
    AssertEq((int)Fl_clear((Fl)7, F_RW), 4);
}

[[cccc::test]] void test_overlapping_composites_do_not_double_print(void) {
    char buf[64];
    Ov_format((Ov)7, buf, sizeof buf);
    AssertEq(strcmp(buf, "O_AB|O_C"), 0);
    Ov_format((Ov)6, buf, sizeof buf);
    AssertEq(strcmp(buf, "O_BC"), 0);
}

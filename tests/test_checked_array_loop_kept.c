// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: --checked-pointers -d
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 11 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 12 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 13 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 14 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 15 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 16 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 17 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 18 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 19 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 20 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 21 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 22 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 23 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 24 0
// CCCC_EXPECT_STDOUT: CHKR +[0-9]+ 25 0
//
// Each function below keeps the range check on its one loop access, found by
// the size of the element it reads: the loop is not a counted loop over a
// local variable that only the increment writes, or its range is not inside
// the extent. None of them is called.
#include <setjmp.h>

typedef struct {
    char b[11];
} E11;
typedef struct {
    char b[12];
} E12;
typedef struct {
    char b[13];
} E13;
typedef struct {
    char b[14];
} E14;
typedef struct {
    char b[15];
} E15;
typedef struct {
    char b[16];
} E16;
typedef struct {
    char b[17];
} E17;
typedef struct {
    char b[18];
} E18;
typedef struct {
    char b[19];
} E19;
typedef struct {
    char b[20];
} E20;
typedef struct {
    char b[21];
} E21;
typedef struct {
    char b[22];
} E22;
typedef struct {
    char b[23];
} E23;
typedef struct {
    char b[24];
} E24;
typedef struct {
    char b[25];
} E25;

int          g_i;
volatile int zero;
E14 ga14     _Checked[5];

int written_in_body(int k) {
    E11 a _Checked[5] = {0};
    int   sum         = 0;
    for (int i = 0; i < 5; i++) {
        sum += a[i].b[0];
        if (k)
            i = 0;
    }
    return sum;
}

int address_taken(int *q) {
    E12 a _Checked[5] = {0};
    int   sum         = 0;
    for (int i = 0; i < 5; i++) {
        q    = &i;
        sum += a[i].b[0];
    }
    return sum + *q;
}

int static_var(void) {
    static int i;
    E13 a      _Checked[5] = {0};
    int        sum         = 0;
    for (i = 0; i < 5; i++)
        sum += a[i].b[0];
    return sum;
}

int global_var(void) {
    int sum = 0;
    for (g_i = 0; g_i < 5; g_i++)
        sum += ga14[g_i].b[0];
    return sum;
}

int volatile_var(void) {
    E15 a _Checked[5] = {0};
    int   sum         = 0;
    for (volatile int i = 0; i < 5; i++)
        sum += a[i].b[0];
    return sum;
}

int inclusive_bound(void) {
    E16 a _Checked[5] = {0};
    int   sum         = 0;
    for (int i = 0; i <= 5; i++)
        sum += a[i].b[0];
    return sum;
}

int bound_past_extent(void) {
    E17 a _Checked[5] = {0};
    int   sum         = 0;
    for (int i = 0; i < 6; i++)
        sum += a[i].b[0];
    return sum;
}

int offset_past_extent(void) {
    E18 a _Checked[5] = {0};
    int   sum         = 0;
    for (int i = 0; i < 5; i++)
        sum += a[i + 1].b[0];
    return sum;
}

int label_in_body(void) {
    E19 a _Checked[5] = {0};
    int   sum         = 0;
    for (int i = 0; i < 5; i++) {
    again:
        sum += a[i].b[0];
        if (zero)
            goto again;
    }
    return sum;
}

int case_in_body(int k) {
    E20 a _Checked[5] = {0};
    int   sum         = 0;
    switch (k) {
        case 0:
            for (int i = 0; i < 5; i++) {
                sum += a[i].b[0];
                case 1:
                    sum++;
            }
    }
    return sum;
}

int pointer_base(E21 *[[cccc::array, cccc::count(5)]] p) {
    int sum = 0;
    for (int i = 0; i < 5; i++)
        sum += p[i].b[0];
    return sum;
}

int narrow_wraps(void) {
    E22 a       _Checked[200] = {0};
    int         sum           = 0;
    signed char c;
    for (c = 0; c < 200; c++)
        sum += a[c].b[0];
    return sum;
}

int access_in_increment(void) {
    E23 a _Checked[5] = {0};
    int   sum         = 0;
    for (int i = 0; i < 5; i++, sum += a[i].b[0])
        ;
    return sum;
}

int set_jmp(jmp_buf env) {
    E24 a _Checked[5] = {0};
    int   sum         = 0;
    if (setjmp(env))
        return -1;
    for (int i = 0; i < 5; i++)
        sum += a[i].b[0];
    return sum;
}

int label_address(void) {
    E25 a _Checked[5] = {0};
    int   sum         = 0;
    void *p           = &&out;
    for (int i = 0; i < 5; i++)
        sum += a[i].b[0];
    goto *p;
out:
    return sum;
}

int main(void) {
    jmp_buf env;
    if (zero) {
        written_in_body(0);
        address_taken(0);
        static_var();
        global_var();
        volatile_var();
        inclusive_bound();
        bound_past_extent();
        offset_past_extent();
        label_in_body();
        case_in_body(0);
        pointer_base(0);
        narrow_wraps();
        access_in_increment();
        set_jmp(env);
        label_address();
    }
    return 42;
}

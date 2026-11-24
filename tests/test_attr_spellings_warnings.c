// CCCC_FLAGS: -Wnonnull -Wsentinel -Wdesignated-init
// CCCC_EXPECT_STDERR: 20 warnings generated
// nonnull, returns_nonnull, sentinel and designated_init take effect in the
// spellings [[gnu::x]], [[__gnu__::__x__]], __attribute__((x)) and @x (four
// warnings each;
// designated_init warns per field).

[[gnu::nonnull]] void n1(int *p);
[[__gnu__::__nonnull__]] void n2(int *p);
__attribute__((nonnull)) void n3(int *p);
@nonnull void n4(int *p);
void n1(int *p) {}
void n2(int *p) {}
void n3(int *p) {}
void n4(int *p) {}

[[gnu::returns_nonnull]] int *r1(void);
[[__gnu__::__returns_nonnull__]] int *r2(void);
__attribute__((returns_nonnull)) int *r3(void);
@returns_nonnull int *r4(void);
int *r1(void) {
    return 0;
}
int *r2(void) {
    return 0;
}
int *r3(void) {
    return 0;
}
int *r4(void) {
    return 0;
}

[[gnu::sentinel]] void s1(int a, ...);
[[__gnu__::__sentinel__]] void s2(int a, ...);
__attribute__((sentinel)) void s3(int a, ...);
@sentinel void s4(int a, ...);
void s1(int a, ...) {}
void s2(int a, ...) {}
void s3(int a, ...) {}
void s4(int a, ...) {}

struct [[gnu::designated_init]] D1 {
    int a, b;
};
struct [[__gnu__::__designated_init__]] D2 {
    int a, b;
};
struct __attribute__((designated_init)) D3 {
    int a, b;
};
struct @designated_init D4 {
    int a, b;
};

int main(void) {
    n1((void *)0);
    n2((void *)0);
    n3((void *)0);
    n4((void *)0);
    r1();
    r2();
    r3();
    r4();
    s1(1, 2, 3);
    s2(1, 2, 3);
    s3(1, 2, 3);
    s4(1, 2, 3);
    struct D1 d1 = {1, 2};
    struct D2 d2 = {1, 2};
    struct D3 d3 = {1, 2};
    struct D4 d4 = {1, 2};
    (void)d1;
    (void)d2;
    (void)d3;
    (void)d4;
    return 42;
}

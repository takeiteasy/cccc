// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __CCCC_CTOR void plain_ctor
// CCCC_EXPECT_STDOUT: __CCCC_CTOR_P\(150\) void prio_ctor
// -m output keeps each constructor and its priority, spelled through the macros.
int ctor_ran;
int ctor_prio_ran;
int order[1];
int order_idx;
__attribute__((constructor)) void plain_ctor(void) {
    ctor_ran = 1;
}
__attribute__((constructor(150))) void prio_ctor(void) {
    ctor_prio_ran = 1;
}
static __attribute__((constructor)) void static_ctor(void) {
    order[order_idx++] = 1;
}
__attribute__((destructor)) void plain_dtor(void) {
    (void)0;
}
int main(void) {
    if (!ctor_ran)
        return 1;
    if (!ctor_prio_ran)
        return 2;
    if (order_idx != 1 || order[0] != 1)
        return 3;
    return 42;
}

typedef struct {
    int a;
    int b;
} DupTypedef1290Smoke;
int dup_typedef_1290_smoke_make_and_use(void) {
    DupTypedef1290Smoke v;
    v.a = 19;
    v.b = 23;
    return v.a + v.b;
}

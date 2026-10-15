// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct __cccc_anon[0-9]+ \{[\s\S]*_Static_assert\(sizeof\(struct __cccc_anon[0-9]+\)
// CCCC_REJECT_STDOUT: \(struct \{
//
// A tagless struct is defined once under a synthesized tag, so every use
// spells that one type. Spelled inline at each use, two copies of the same
// struct are different types to the host compiler and `p[k++] = p[i]` fails.

typedef struct {
    int pad;
    struct {
        int   loc;
        void *fn;
    }  *p;
    int n;
} C;

typedef struct {
    C c;
} VM;

static struct {
    int   loc;
    void *fn;
} buf[4];

int main(void) {
    VM  v          = {0};
    VM *vm         = &v;
    vm->c.p        = buf;
    vm->c.p[0].loc = 40;
    vm->c.p[1].loc = 2;
    int kept       = 0;
    for (int i = 0; i < 2; i++)
        vm->c.p[kept++] = vm->c.p[i];
    return vm->c.p[0].loc + vm->c.p[1].loc;
}

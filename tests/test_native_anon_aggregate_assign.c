// -c=native: assigning one element of a tagless-struct array to another builds
// and runs
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

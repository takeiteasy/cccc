// A pointer in a union can't be a slot of its own; a startup constructor
// stores it.
int x = 7;

union WithPointer {
    int *p;
    long l;
    _BitInt(256) f : 100;
};

union WithPointer g = {.p = &x};

int main(void) {
    return g.p == &x && *g.p == 7 ? 42 : 1;
}

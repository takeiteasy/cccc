// An attribute name with no handler is dropped, as on any other declaration.
struct P {
    int x @no_such_handler;
    int y __attribute__((no_such_handler));
};

int main(void) {
    struct P p = {40, 2};
    return p.x + p.y;
}

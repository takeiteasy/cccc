// -c=native, a block capture whose own struct/union type is declared inside a
// function round-trips as real C via file-scope hoisting -- by-value capture of
// a tagged local struct, pointer-to-local-struct capture, a tagless local
// aggregate, and a nested local type (transitive hoisting), all in one program
int main(void) {
    struct Q {
        int y;
    };
    struct P {
        struct Q q;
    };
    struct P  p  = {{1}};
    struct P *pp = &p;
    struct {
        int z;
    } t             = {2};

    int (^b1)(void) = ^{
      return p.q.y;
    };
    int (^b2)(void) = ^{
      return pp->q.y;
    };
    int (^b3)(void) = ^{
      return t.z;
    };

    return b1() + b2() + b3() + 38;
}

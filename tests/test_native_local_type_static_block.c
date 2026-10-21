// A static of a function-local type (block) round-trips under -c=native.
int main(void) {
    struct S {
        int v;
    };
    static struct S a = {40};
    int (^blk)(void)  = ^{
      return a.v + 2;
    };
    return blk();
}

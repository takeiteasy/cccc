// #994: block capture of a by-value aggregate larger than 8 bytes was
// silently truncated to its first word -- the block descriptor was a flat
// one-8-byte-slot-per-capture array and the capture-copy loop always did
// exactly one 8-byte load+store regardless of the capture's real size.
// This is the ticket's own minimal repro: struct S is 16 bytes, so a
// single-word copy dropped b entirely and read back 0/garbage.
struct S {
    long a;
    long b;
};

int main(void) {
    struct S t;
    t.a              = 1;
    t.b              = 42;
    int (^blk)(void) = ^{
      return (int)t.b;
    };
    return blk();
}

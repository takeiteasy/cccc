// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: extern void free\(void \*\);
//
// #990: Block_release(b) falls back to vm->compiler.builtin_free (#458)
// when no free() prototype is in scope. That Obj has no obj->tok (it was
// synthesized, not parsed), so the function-prototype pass's from_primary
// filter silently dropped it -- the generated C called an undeclared
// free(). serialize_block_preamble now emits an explicit
// `extern void free(void *);` whenever Block_release is reachable.
// Deliberately no <stdlib.h> here -- including it would mask the gap this
// test exists to catch.

typedef int (^IntBlock)(void);

IntBlock make_adder(int x) {
    IntBlock inner = ^{
      return x + 1;
    };
    return Block_copy(inner);
}

int main(void) {
    IntBlock a = make_adder(41);
    int      r = a();
    Block_release(a);
    return r == 42 ? 42 : 1;
}

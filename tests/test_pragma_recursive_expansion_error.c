// EXPECT_COMPILE_ERROR
// Recursive macro expansion should report a compiler error, not crash.

#pragma macro
_Node *recurse_forever(void) {
    return _QUOTE("recurse_forever()");
}

int main(void) {
    return recurse_forever();
}

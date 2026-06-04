// EXPECT_COMPILE_ERROR
[[jcc::macro(inline)]]
_Node *ninth(_Node *a0, _Node *a1, _Node *a2, _Node *a3,
             _Node *a4, _Node *a5, _Node *a6, _Node *a7,
             _Node *a8) {
    return a8;
}

int main() { return ninth(1, 2, 3, 4, 5, 6, 7, 8, 42); }

// EXPECT_COMPILE_ERROR
#pragma macro
JCC_Node *ninth(JCC_Node *a0, JCC_Node *a1, JCC_Node *a2, JCC_Node *a3,
                JCC_Node *a4, JCC_Node *a5, JCC_Node *a6, JCC_Node *a7,
                JCC_Node *a8) {
    return a8;
}

int main() { return ninth(1, 2, 3, 4, 5, 6, 7, 8, 42); }

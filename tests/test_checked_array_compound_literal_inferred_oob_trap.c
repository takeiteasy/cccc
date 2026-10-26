// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// An inferred-extent compound literal of checked array type is checked
// against the initializer count.

int main(void) {
    volatile int i = 2;
    return (int _Checked[]){1, 2}[i];
}

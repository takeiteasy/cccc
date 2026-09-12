// EXPECT_COMPILE_ERROR
// #486: [[cccc::assume]]/[[cccc::dynamic]] require the pointer to also be
// declared [[cccc::single]], [[cccc::array]] or [[cccc::ntarray]] -- there
// is no checked kind here for the claim to attach to.

int main(void) {
    int  x = 5;
    int *p = (int *[[cccc::assume]])&x;
    return *p;
}

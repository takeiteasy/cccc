// CCCC_FLAGS: -Wshadow
// CCCC_REJECT_STDERR: shadows an outer variable
// A declarator name never carries over to later variables of the same type.

int main(void) {
    int outer = 1;
    {
        int leftover = 2;
        auto deduced = 5;
        (void)leftover;
        (void)deduced;
    }
    {
        int y = 4;
        {
            auto z = 6;
            (void)z;
        }
        (void)y;
    }
    return outer + 41;
}

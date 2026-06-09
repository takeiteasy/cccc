// CCCC_FLAGS: --std=c23
// Test #embed with all parameters: prefix, suffix, limit, if_empty
int main() {
    // test_data.bin contains 3 bytes, but we limit to 2
    unsigned char data[] = {
        #embed "embed_data/test_data.bin" prefix(1,) suffix(,9) limit(2) if_empty(0)
    };

    // Should have: 1, byte0, byte1, 9
    // if_empty is ignored because file is not empty after limit
    int size = sizeof(data);
    if (size != 4) {
        return 1;  // Wrong size
    }

    // Verify prefix
    if (data[0] != 1) {
        return 2;  // Prefix incorrect
    }

    // Verify suffix
    if (data[3] != 9) {
        return 3;  // Suffix incorrect
    }

    return 42;  // Success
}

// CCCC_FLAGS: --std=c23
// Test #embed with both prefix and suffix parameters
int main() {
    // test_data.bin contains 3 bytes
    unsigned char data[] = {
        #embed "embed_data/test_data.bin" prefix(1, 2,) suffix(, 9, 10)
    };

    // Should have: 1, 2, byte0, byte1, byte2, 9, 10
    int size = sizeof(data);
    if (size != 7) {
        return 1;  // Wrong size
    }

    // Verify prefix values
    if (data[0] != 1 || data[1] != 2) {
        return 2;  // Prefix values incorrect
    }

    // Verify suffix values
    if (data[5] != 9 || data[6] != 10) {
        return 3;  // Suffix values incorrect
    }

    return 42;  // Success
}

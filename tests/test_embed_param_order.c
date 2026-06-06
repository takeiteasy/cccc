// JCC_FLAGS: --std=c23
// Test #embed parameter order independence
// Parameters should work in any order
int main() {
    // test_data.bin contains 3 bytes, but we limit to 2
    // Parameters specified in non-standard order
    unsigned char data[] = {
        #embed "embed_data/test_data.bin" suffix(, 88) prefix(77,) limit(2)
    };

    // Should have: 77, byte0, byte1, 88
    int size = sizeof(data);
    if (size != 4) {
        return 1;  // Wrong size
    }

    // Verify prefix
    if (data[0] != 77) {
        return 2;  // Prefix incorrect
    }

    // Verify suffix
    if (data[3] != 88) {
        return 3;  // Suffix incorrect
    }

    return 42;  // Success
}

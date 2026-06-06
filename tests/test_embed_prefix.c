// JCC_FLAGS: --std=c23
// Test #embed with prefix parameter
int main() {
    // test_data.bin contains 3 bytes
    unsigned char data[] = {
        #embed "embed_data/test_data.bin" prefix(100, 101,)
    };

    // Should have: 100, 101, byte0, byte1, byte2
    int size = sizeof(data);
    if (size != 5) {
        return 1;  // Wrong size
    }

    // Verify prefix values
    if (data[0] != 100 || data[1] != 101) {
        return 2;  // Prefix values incorrect
    }

    return 42;  // Success
}

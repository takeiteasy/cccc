// JCC_FLAGS: --std=c23
// Test #embed with suffix parameter
int main() {
    // test_data.bin contains 3 bytes
    unsigned char data[] = {
        #embed "embed_data/test_data.bin" suffix(, 200, 201)
    };

    // Should have: byte0, byte1, byte2, 200, 201
    int size = sizeof(data);
    if (size != 5) {
        return 1;  // Wrong size
    }

    // Verify suffix values
    if (data[3] != 200 || data[4] != 201) {
        return 2;  // Suffix values incorrect
    }

    return 42;  // Success
}

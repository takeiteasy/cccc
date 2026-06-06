// JCC_FLAGS: --std=c23
// Test #embed with if_empty parameter on empty file
int main() {
    // empty_file.bin is empty (0 bytes)
    unsigned char data[] = {
        #embed "embed_data/empty_file.bin" if_empty(42)
    };

    // Should have just the if_empty value: 42
    int size = sizeof(data);
    if (size != 1) {
        return 1;  // Wrong size
    }

    // Verify if_empty value
    if (data[0] != 42) {
        return 2;  // if_empty value incorrect
    }

    return 42;  // Success
}

// JCC_FLAGS: --std=c23
// Test #embed with limit(0) and if_empty - limit(0) makes file "empty"
int main() {
    // test_data.bin contains 3 bytes, but limit(0) makes it empty
    unsigned char data[] = {
        #embed "embed_data/test_data.bin" limit(0) if_empty(99)
    };

    // Should have just the if_empty value: 99
    // Because limit(0) results in 0 bytes, triggering if_empty
    int size = sizeof(data);
    if (size != 1) {
        return 1;  // Wrong size
    }

    // Verify if_empty value
    if (data[0] != 99) {
        return 2;  // if_empty value incorrect
    }

    return 42;  // Success
}

// CCCC_FLAGS: --std=c23
int main() {
    unsigned char data[] = {
        #embed "embed_data/test_data.bin" limit(2)
    };

    int size = sizeof(data);
    if (size != 2) {
        return 1;
    }

    int sum = data[0] + data[1] + 12;
    return sum;
}

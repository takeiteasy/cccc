// JCC_FLAGS: -std=c23
int main() {
    unsigned char data[] = {
        #embed "embed_data/test_data.bin"
    };

    int size = sizeof(data);
    if (size != 3) {
        return 1;
    }

    int sum = data[0] + data[1] + data[2];
    return sum;
}

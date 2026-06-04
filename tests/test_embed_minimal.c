// JCC_FLAGS: -std=c23
int main() {
    unsigned char data[] = {
#embed "embed_data/test_data.bin"
    };
    return data[0] + data[1] + data[2];
}

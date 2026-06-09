// CCCC_FLAGS: --std=c23
int main() {
    unsigned char data[] = {
        #embed "embed_data/empty_file.bin"
        42
    };

    int size = sizeof(data);
    if (size != 1) {
        return 1;
    }

    return data[0];
}

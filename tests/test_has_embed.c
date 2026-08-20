int main() {
    int result = 0;

#if __has_embed("embed_data/test_data.bin") == 1
    result += 10;
#endif

#if __has_embed("embed_data/empty_file.bin") == 2
    result += 20;
#endif

#if __has_embed("nonexistent_file.bin") == 0
    result += 12;
#endif

    return result;
}

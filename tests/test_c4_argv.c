static int ends_with(const char *s, const char *suffix) {
    const char *p = s;
    const char *q = suffix;
    int len = 0;
    int suffix_len = 0;

    while (*p++) len++;
    while (*q++) suffix_len++;
    if (len < suffix_len)
        return 0;

    s += len - suffix_len;
    while (*suffix) {
        if (*s++ != *suffix++)
            return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 1)
        return ends_with(argv[0], ".c4") ? 42 : 1;
    if (argc == 2)
        return ends_with(argv[1], ".c") ? 42 : 2;
    return 3;
}

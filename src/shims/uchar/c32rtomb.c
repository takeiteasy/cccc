size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps) {
    return wcrtomb(s, (wchar_t)c32, ps);
}

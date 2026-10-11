size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps) {
    return wcrtomb(s, (wchar_t)c16, ps);
}

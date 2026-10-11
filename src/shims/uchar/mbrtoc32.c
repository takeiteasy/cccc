size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps) {
    wchar_t wc;
    size_t rc = mbrtowc(&wc, s, n, ps);
    if (rc == (size_t)-1 || rc == (size_t)-2 || rc == 0)
        return rc;
    if (pc32) *pc32 = (char32_t)wc;
    return rc;
}

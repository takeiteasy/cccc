size_t c8rtomb(char *s, char8_t c8, mbstate_t *ps) {
    static mbstate_t internal_state;
    if (!ps) ps = &internal_state;
    if (!s) { __builtin_memset(ps, 0, sizeof(*ps)); return 0; }
    __cccc_c8out_state st;
    __builtin_memcpy(&st, ps, sizeof(st));
    if (c8 == 0) {
        if (st.len != 0) { errno = EILSEQ; return (size_t)-1; }
        mbstate_t wcs;
        __builtin_memset(&wcs, 0, sizeof(wcs));
        return wcrtomb(s, L'\0', &wcs);
    }
    if (st.len == 0) {
        unsigned need;
        if ((c8 & 0x80) == 0x00) need = 1;
        else if ((c8 & 0xE0) == 0xC0) need = 2;
        else if ((c8 & 0xF0) == 0xE0) need = 3;
        else if ((c8 & 0xF8) == 0xF0) need = 4;
        else { errno = EILSEQ; return (size_t)-1; }
        st.need = (unsigned char)need;
    } else if ((c8 & 0xC0) != 0x80) {
        __builtin_memset(ps, 0, sizeof(*ps));
        errno = EILSEQ;
        return (size_t)-1;
    }
    st.buf[st.len++] = (unsigned char)c8;
    if (st.len < st.need) {
        __builtin_memcpy(ps, &st, sizeof(st));
        return 0;
    }
    unsigned cp;
    int ok = __cccc_utf8_decode(st.buf, st.len, &cp) == 0;
    __builtin_memset(ps, 0, sizeof(*ps));
    if (!ok) { errno = EILSEQ; return (size_t)-1; }
    mbstate_t wcs;
    __builtin_memset(&wcs, 0, sizeof(wcs));
    return wcrtomb(s, (wchar_t)cp, &wcs);
}

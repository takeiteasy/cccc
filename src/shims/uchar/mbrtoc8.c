size_t mbrtoc8(char8_t *pc8, const char *s, size_t n, mbstate_t *ps) {
    static mbstate_t internal_state;
    if (!ps) ps = &internal_state;
    __cccc_c8state st;
    __builtin_memcpy(&st, ps, sizeof(st));
    if (st.magic == __CCCC_C8STATE_MAGIC && st.len > 0) {
        if (pc8) *pc8 = st.buf[st.pos];
        st.pos++;
        st.len--;
        if (st.len == 0) __builtin_memset(ps, 0, sizeof(*ps));
        else __builtin_memcpy(ps, &st, sizeof(st));
        return (size_t)-3;
    }
    wchar_t wc;
    size_t rc = mbrtowc(&wc, s, n, ps);
    if (rc == (size_t)-1 || rc == (size_t)-2) return rc;
    if (rc == 0) { if (pc8) *pc8 = 0; return 0; }
    unsigned char enc[4];
    unsigned elen = __cccc_utf8_encode(enc, (unsigned)wc);
    if (elen == 0) { errno = EILSEQ; return (size_t)-1; }
    if (pc8) *pc8 = enc[0];
    if (elen > 1) {
        __cccc_c8state newst;
        __builtin_memset(&newst, 0, sizeof(newst));
        newst.magic = __CCCC_C8STATE_MAGIC;
        __builtin_memcpy(newst.buf, enc + 1, elen - 1);
        newst.len = (unsigned char)(elen - 1);
        newst.pos = 0;
        __builtin_memcpy(ps, &newst, sizeof(newst));
    }
    return rc;
}

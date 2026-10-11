static size_t __cccc_native_confstr(int name, char *buf, size_t len) {
    switch (name) {
#ifdef _CS_PATH
        case 1:
            return confstr(_CS_PATH, buf, len);
#endif
        default:
            errno = EINVAL;
            return 0;
    }
}

static long __cccc_native_pathconf(const char *path, int name) {
    int host = __cccc_native_pc_map(name);
    if (host < 0) {
        errno = EINVAL;
        return -1;
    }
    return (long)pathconf(path, host);
}

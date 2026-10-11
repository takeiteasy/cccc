static long __cccc_native_fpathconf(int fd, int name) {
    int host = __cccc_native_pc_map(name);
    if (host < 0) {
        errno = EINVAL;
        return -1;
    }
    return (long)fpathconf(fd, host);
}

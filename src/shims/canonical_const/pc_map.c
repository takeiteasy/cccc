static int __cccc_native_pc_map(int name) {
    switch (name) {
        case 1:
#ifdef _PC_LINK_MAX
            return _PC_LINK_MAX;
#else
            break;
#endif
        case 2:
#ifdef _PC_MAX_CANON
            return _PC_MAX_CANON;
#else
            break;
#endif
        case 3:
#ifdef _PC_MAX_INPUT
            return _PC_MAX_INPUT;
#else
            break;
#endif
        case 4:
#ifdef _PC_NAME_MAX
            return _PC_NAME_MAX;
#else
            break;
#endif
        case 5:
#ifdef _PC_PATH_MAX
            return _PC_PATH_MAX;
#else
            break;
#endif
        case 6:
#ifdef _PC_PIPE_BUF
            return _PC_PIPE_BUF;
#else
            break;
#endif
        case 7:
#ifdef _PC_CHOWN_RESTRICTED
            return _PC_CHOWN_RESTRICTED;
#else
            break;
#endif
        case 8:
#ifdef _PC_NO_TRUNC
            return _PC_NO_TRUNC;
#else
            break;
#endif
        case 9:
#ifdef _PC_VDISABLE
            return _PC_VDISABLE;
#else
            break;
#endif
    }
    return -1;
}

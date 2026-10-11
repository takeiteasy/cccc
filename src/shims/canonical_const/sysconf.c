static long __cccc_native_sysconf(int name) {
    switch (name) {
        case 10:
            return 200809L;
        case 17:
            return 200809L;
        case 18:
            return 700L;
#ifdef _SC_ARG_MAX
        case 1:
            return (long)sysconf(_SC_ARG_MAX);
#endif
#ifdef _SC_CHILD_MAX
        case 2:
            return (long)sysconf(_SC_CHILD_MAX);
#endif
#ifdef _SC_CLK_TCK
        case 3:
            return (long)sysconf(_SC_CLK_TCK);
#endif
#ifdef _SC_NGROUPS_MAX
        case 4:
            return (long)sysconf(_SC_NGROUPS_MAX);
#endif
#ifdef _SC_OPEN_MAX
        case 5:
            return (long)sysconf(_SC_OPEN_MAX);
#endif
#ifdef _SC_STREAM_MAX
        case 6:
            return (long)sysconf(_SC_STREAM_MAX);
#endif
#ifdef _SC_TZNAME_MAX
        case 7:
            return (long)sysconf(_SC_TZNAME_MAX);
#endif
#ifdef _SC_JOB_CONTROL
        case 8:
            return (long)sysconf(_SC_JOB_CONTROL);
#endif
#ifdef _SC_SAVED_IDS
        case 9:
            return (long)sysconf(_SC_SAVED_IDS);
#endif
#ifdef _SC_PAGESIZE
        case 11:
            return (long)sysconf(_SC_PAGESIZE);
#endif
#ifdef _SC_NPROCESSORS_CONF
        case 12:
            return (long)sysconf(_SC_NPROCESSORS_CONF);
#endif
#ifdef _SC_NPROCESSORS_ONLN
        case 13:
            return (long)sysconf(_SC_NPROCESSORS_ONLN);
#endif
#ifdef _SC_PHYS_PAGES
        case 14:
            return (long)sysconf(_SC_PHYS_PAGES);
#endif
#ifdef _SC_LINE_MAX
        case 15:
            return (long)sysconf(_SC_LINE_MAX);
#endif
#ifdef _SC_RE_DUP_MAX
        case 16:
            return (long)sysconf(_SC_RE_DUP_MAX);
#endif
#ifdef _SC_HOST_NAME_MAX
        case 19:
            return (long)sysconf(_SC_HOST_NAME_MAX);
#endif
#ifdef _SC_LOGIN_NAME_MAX
        case 20:
            return (long)sysconf(_SC_LOGIN_NAME_MAX);
#endif
#ifdef _SC_TTY_NAME_MAX
        case 21:
            return (long)sysconf(_SC_TTY_NAME_MAX);
#endif
#ifdef _SC_SYMLOOP_MAX
        case 22:
            return (long)sysconf(_SC_SYMLOOP_MAX);
#endif
#ifdef _SC_ATEXIT_MAX
        case 23:
            return (long)sysconf(_SC_ATEXIT_MAX);
#endif
#ifdef _SC_IOV_MAX
        case 24:
            return (long)sysconf(_SC_IOV_MAX);
#endif
#ifdef _SC_GETPW_R_SIZE_MAX
        case 25:
            return (long)sysconf(_SC_GETPW_R_SIZE_MAX);
#endif
#ifdef _SC_GETGR_R_SIZE_MAX
        case 26:
            return (long)sysconf(_SC_GETGR_R_SIZE_MAX);
#endif
#ifdef _SC_MONOTONIC_CLOCK
        case 27:
            return (long)sysconf(_SC_MONOTONIC_CLOCK);
#endif
        default:
            errno = EINVAL;
            return -1;
    }
}

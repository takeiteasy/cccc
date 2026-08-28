// -c=native canonical-constant translation shims (#1146): nl_langinfo,
// setlocale/newlocale, sched_get_priority_min/max, sysconf/pathconf/
// fpathconf/confstr. Each wrapper translates CCCC's canonical numbering
// to the host's real values before calling the real libc function.
//
// Source of truth for the text tools/gen_shims.py embeds into
// src/shims.inc. NOT COMPILED. Gating and rationale live in the
// matching serialize_*_shims() in src/serialize_shims.c.

// >>> shim: nl_item_xlate
static int __cccc_native_guest_to_host_nl_item(int   guest_item,
                                               long *host_item) {
    long v = (long)guest_item;
#ifdef __APPLE__
    // Same accept-set validation as guest_to_host_nl_item
    // (src/stdlib/posix_lang.c, #1148): CCCC's canonical numbering equals
    // macOS's real numbering, so no translation is needed, but an
    // unrecognized item (the 45-49/54/55 holes, negatives, or > 56) must
    // still be rejected rather than forwarded to the host raw.
    if ((v >= 0 && v <= 44) || (v >= 50 && v <= 53) || v == 56) {
        *host_item = guest_item;
        return 1;
    }
    return 0;
#else
    switch (v) {
        case 0:
            *host_item = 14;
            break;
        case 1:
            *host_item = 131112;
            break;
        case 2:
            *host_item = 131113;
            break;
        case 3:
            *host_item = 131114;
            break;
        case 4:
            *host_item = 131115;
            break;
        case 5:
            *host_item = 131110;
            break;
        case 6:
            *host_item = 131111;
            break;
        case 50:
            *host_item = 65536;
            break;
        case 51:
            *host_item = 65537;
            break;
        case 52:
            *host_item = 327680;
            break;
        case 53:
            *host_item = 327681;
            break;
        case 56:
            *host_item = 262159;
            break;
        default:
            if (v >= 7 && v <= 13)
                *host_item = 131079 + (v - 7);
            else if (v >= 14 && v <= 20)
                *host_item = 131072 + (v - 14);
            else if (v >= 21 && v <= 32)
                *host_item = 131098 + (v - 21);
            else if (v >= 33 && v <= 44)
                *host_item = 131086 + (v - 33);
            else
                return 0;
            break;
    }
    return 1;
#endif
}
// <<< shim

// >>> shim: nl_langinfo
static char *__cccc_native_nl_langinfo(nl_item guest_item) {
    long host_item;
    if (!__cccc_native_guest_to_host_nl_item((int)guest_item, &host_item))
        return "";
    return nl_langinfo((nl_item)host_item);
}
// <<< shim

// >>> shim: nl_langinfo_l
static char *__cccc_native_nl_langinfo_l(nl_item guest_item, locale_t loc) {
    long host_item;
    if (!__cccc_native_guest_to_host_nl_item((int)guest_item, &host_item))
        return "";
    return nl_langinfo_l((nl_item)host_item, loc);
}
// <<< shim

// >>> shim: setlocale
static int __cccc_native_guest_to_host_lc(int guest_category) {
    switch (guest_category) {
        case 0:
            return LC_ALL;
        case 1:
            return LC_COLLATE;
        case 2:
            return LC_CTYPE;
        case 3:
            return LC_MONETARY;
        case 4:
            return LC_NUMERIC;
        case 5:
            return LC_TIME;
        case 6:
            return LC_MESSAGES;
        default:
            return guest_category;
    }
}
static char *__cccc_native_setlocale(int guest_category, const char *locale) {
    return setlocale(__cccc_native_guest_to_host_lc(guest_category), locale);
}
// <<< shim

// >>> shim: newlocale
static int __cccc_native_guest_to_host_lc_mask(int guest_mask) {
    if (guest_mask == 0x3f)
        return LC_ALL_MASK;
    int host_mask = 0;
    if (guest_mask & (1 << 0))
        host_mask |= LC_COLLATE_MASK;
    if (guest_mask & (1 << 1))
        host_mask |= LC_CTYPE_MASK;
    if (guest_mask & (1 << 2))
        host_mask |= LC_MESSAGES_MASK;
    if (guest_mask & (1 << 3))
        host_mask |= LC_MONETARY_MASK;
    if (guest_mask & (1 << 4))
        host_mask |= LC_NUMERIC_MASK;
    if (guest_mask & (1 << 5))
        host_mask |= LC_TIME_MASK;
    return host_mask;
}
static locale_t __cccc_native_newlocale(int guest_mask, const char *locale,
                                        locale_t base) {
    return newlocale(__cccc_native_guest_to_host_lc_mask(guest_mask), locale,
                     base);
}
// <<< shim

// >>> shim: sched_batch_idle_defs
#ifdef __linux__
#ifndef SCHED_BATCH
#define SCHED_BATCH 3
#endif
#ifndef SCHED_IDLE
#define SCHED_IDLE 5
#endif
#endif
// <<< shim

// >>> shim: sched_policy_xlate
static int __cccc_native_guest_to_host_sched_policy(int guest_policy) {
    switch (guest_policy) {
        case 0:
            return SCHED_OTHER;
        case 1:
            return SCHED_FIFO;
        case 2:
            return SCHED_RR;
#ifdef __linux__
        case 3:
            return SCHED_BATCH;
        case 5:
            return SCHED_IDLE;
#endif
        default:
            return guest_policy;
    }
}
// <<< shim

// >>> shim: sched_get_priority_min
static int __cccc_native_sched_get_priority_min(int policy) {
    return sched_get_priority_min(
        __cccc_native_guest_to_host_sched_policy(policy));
}
// <<< shim

// >>> shim: sched_get_priority_max
static int __cccc_native_sched_get_priority_max(int policy) {
    return sched_get_priority_max(
        __cccc_native_guest_to_host_sched_policy(policy));
}
// <<< shim

// >>> shim: sysconf
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
// <<< shim

// >>> shim: pathconf
static long __cccc_native_pathconf(const char *path, int name) {
    switch (name) {
        case 1:
#ifdef _PC_LINK_MAX
            return (long)pathconf(path, _PC_LINK_MAX);
#else
            break;
#endif
        case 2:
#ifdef _PC_MAX_CANON
            return (long)pathconf(path, _PC_MAX_CANON);
#else
            break;
#endif
        case 3:
#ifdef _PC_MAX_INPUT
            return (long)pathconf(path, _PC_MAX_INPUT);
#else
            break;
#endif
        case 4:
#ifdef _PC_NAME_MAX
            return (long)pathconf(path, _PC_NAME_MAX);
#else
            break;
#endif
        case 5:
#ifdef _PC_PATH_MAX
            return (long)pathconf(path, _PC_PATH_MAX);
#else
            break;
#endif
        case 6:
#ifdef _PC_PIPE_BUF
            return (long)pathconf(path, _PC_PIPE_BUF);
#else
            break;
#endif
        case 7:
#ifdef _PC_CHOWN_RESTRICTED
            return (long)pathconf(path, _PC_CHOWN_RESTRICTED);
#else
            break;
#endif
        case 8:
#ifdef _PC_NO_TRUNC
            return (long)pathconf(path, _PC_NO_TRUNC);
#else
            break;
#endif
        case 9:
#ifdef _PC_VDISABLE
            return (long)pathconf(path, _PC_VDISABLE);
#else
            break;
#endif
    }
    errno = EINVAL;
    return -1;
}
// <<< shim

// >>> shim: fpathconf
static long __cccc_native_fpathconf(int fd, int name) {
    switch (name) {
        case 1:
#ifdef _PC_LINK_MAX
            return (long)fpathconf(fd, _PC_LINK_MAX);
#else
            break;
#endif
        case 2:
#ifdef _PC_MAX_CANON
            return (long)fpathconf(fd, _PC_MAX_CANON);
#else
            break;
#endif
        case 3:
#ifdef _PC_MAX_INPUT
            return (long)fpathconf(fd, _PC_MAX_INPUT);
#else
            break;
#endif
        case 4:
#ifdef _PC_NAME_MAX
            return (long)fpathconf(fd, _PC_NAME_MAX);
#else
            break;
#endif
        case 5:
#ifdef _PC_PATH_MAX
            return (long)fpathconf(fd, _PC_PATH_MAX);
#else
            break;
#endif
        case 6:
#ifdef _PC_PIPE_BUF
            return (long)fpathconf(fd, _PC_PIPE_BUF);
#else
            break;
#endif
        case 7:
#ifdef _PC_CHOWN_RESTRICTED
            return (long)fpathconf(fd, _PC_CHOWN_RESTRICTED);
#else
            break;
#endif
        case 8:
#ifdef _PC_NO_TRUNC
            return (long)fpathconf(fd, _PC_NO_TRUNC);
#else
            break;
#endif
        case 9:
#ifdef _PC_VDISABLE
            return (long)fpathconf(fd, _PC_VDISABLE);
#else
            break;
#endif
    }
    errno = EINVAL;
    return -1;
}
// <<< shim

// >>> shim: confstr
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
// <<< shim

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

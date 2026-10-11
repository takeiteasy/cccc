static char *__cccc_native_nl_langinfo_l(nl_item guest_item, locale_t loc) {
    long host_item;
    if (!__cccc_native_guest_to_host_nl_item((int)guest_item, &host_item))
        return "";
    return nl_langinfo_l((nl_item)host_item, loc);
}

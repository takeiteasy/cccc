static char *__cccc_native_nl_langinfo(nl_item guest_item) {
    long host_item;
    if (!__cccc_native_guest_to_host_nl_item((int)guest_item, &host_item))
        return "";
    return nl_langinfo((nl_item)host_item);
}

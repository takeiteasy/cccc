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

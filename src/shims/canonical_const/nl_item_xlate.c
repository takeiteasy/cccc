static int __cccc_native_guest_to_host_nl_item(int   guest_item,
                                               long *host_item) {
    long v = (long)guest_item;
#ifdef __APPLE__
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

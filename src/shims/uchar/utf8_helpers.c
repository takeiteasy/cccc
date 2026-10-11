typedef struct { unsigned char magic; unsigned char buf[4];
                 unsigned char len; unsigned char pos; } __cccc_c8state;
#define __CCCC_C8STATE_MAGIC 0xC8
_Static_assert(sizeof(mbstate_t) >= sizeof(__cccc_c8state),
               "cccc: host mbstate_t too small to hold __cccc_c8state");
typedef struct { unsigned char buf[4]; unsigned char len;
                 unsigned char need; } __cccc_c8out_state;
_Static_assert(sizeof(mbstate_t) >= sizeof(__cccc_c8out_state),
               "cccc: host mbstate_t too small to hold __cccc_c8out_state");
static unsigned __cccc_utf8_encode(unsigned char out[4], unsigned cp) {
    if (cp <= 0x7F) { out[0] = (unsigned char)cp; return 1; }
    if (cp <= 0x7FF) {
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
    if (cp <= 0xFFFF) {
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (unsigned char)(0xF0 | (cp >> 18));
        out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (unsigned char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}
static int __cccc_utf8_decode(const unsigned char *buf, unsigned len, unsigned *out) {
    unsigned cp;
    switch (len) {
        case 1:
            if (buf[0] & 0x80) return -1;
            cp = buf[0];
            break;
        case 2:
            if ((buf[1] & 0xC0) != 0x80) return -1;
            cp = (unsigned)(buf[0] & 0x1F) << 6 | (buf[1] & 0x3F);
            if (cp < 0x80) return -1;
            break;
        case 3:
            if ((buf[1] & 0xC0) != 0x80 || (buf[2] & 0xC0) != 0x80) return -1;
            cp = (unsigned)(buf[0] & 0x0F) << 12 | (unsigned)(buf[1] & 0x3F) << 6 | (buf[2] & 0x3F);
            if (cp < 0x800) return -1;
            if (cp >= 0xD800 && cp <= 0xDFFF) return -1;
            break;
        case 4:
            if ((buf[1] & 0xC0) != 0x80 || (buf[2] & 0xC0) != 0x80 ||
                (buf[3] & 0xC0) != 0x80) return -1;
            cp = (unsigned)(buf[0] & 0x07) << 18 | (unsigned)(buf[1] & 0x3F) << 12 |
                 (unsigned)(buf[2] & 0x3F) << 6 | (buf[3] & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF) return -1;
            break;
        default:
            return -1;
    }
    *out = cp;
    return 0;
}

#if !defined(__GLIBC__)
#define __CCCC_NEED_UCHAR16_32_SHIM 1
#elif !__GLIBC_PREREQ(2, 16)
#define __CCCC_NEED_UCHAR16_32_SHIM 1
#endif
#ifdef __CCCC_NEED_UCHAR16_32_SHIM

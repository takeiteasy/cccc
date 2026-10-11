#if !defined(__GLIBC__)
#define __CCCC_NEED_UCHAR8_SHIM 1
#elif !__GLIBC_PREREQ(2, 36)
#define __CCCC_NEED_UCHAR8_SHIM 1
#endif
#ifdef __CCCC_NEED_UCHAR8_SHIM

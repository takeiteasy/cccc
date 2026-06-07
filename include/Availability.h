/*
 * JCC stub for Apple's Availability.h
 * 
 * This header provides empty definitions for Apple's availability macros.
 * These macros are used in macOS/iOS SDK headers to mark API availability
 * but are not needed for actual compilation - they just add metadata.
 * 
 * Since JCC doesn't support the __attribute__((availability(...))) syntax,
 * we define all the macros as empty.
 */

#ifndef __AVAILABILITY__
#define __AVAILABILITY__

/*
 * __has_* feature testing macros are provided by JCC's preprocessor.  The
 * guarded fallbacks below are only for non-JCC preprocessing environments.
 */
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#ifndef __has_extension
#define __has_extension(x) 0
#endif
#ifndef __has_attribute
#define __has_attribute(x) 0
#endif
#ifndef __has_include
#define __has_include(x) 0
#endif
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif
#ifndef __has_c_attribute
#define __has_c_attribute(x) 0
#endif
#ifndef __has_cpp_attribute
#define __has_cpp_attribute(x) 0
#endif

/* Header inline macros (needed for sys/_types/_fd_def.h and others) */
#ifndef __header_inline
#define __header_inline static inline
#endif
#ifndef __header_always_inline
#define __header_always_inline static inline
#endif

/* Strip __attribute__ specifications - JCC doesn't parse all attribute positions.
 * These are optimizer hints and not required for correct compilation. */
#ifndef __attribute__
#define __attribute__(x)
#endif

/* GCC keyword aliases that may not have trailing underscores */
#ifndef __signed
#define __signed signed
#endif

/* C linkage macros (normally defined in sys/cdefs.h but may not be available early) */
#ifndef __BEGIN_DECLS
#define __BEGIN_DECLS
#endif
#ifndef __END_DECLS
#define __END_DECLS
#endif

/* sys/cdefs.h attribute macros - these use __attribute__ which we strip */
#ifndef __dead2
#define __dead2
#endif
#ifndef __pure2
#define __pure2
#endif
#ifndef __unused
#define __unused
#endif
#ifndef __used
#define __used
#endif
#ifndef __cold
#define __cold
#endif
#ifndef __deprecated
#define __deprecated
#endif
#ifndef __deprecated_msg
#define __deprecated_msg(msg)
#endif
#ifndef __unavailable
#define __unavailable
#endif
#ifndef __warn_result
#define __warn_result
#endif
#ifndef __restrict
#define __restrict restrict
#endif

/* API deprecation target version (large number = never deprecated) */
#ifndef __API_TO_BE_DEPRECATED
#define __API_TO_BE_DEPRECATED 100000
#endif

/* Empty definitions for availability macros */
#define __API_AVAILABLE(...)
#define __API_AVAILABLE_BEGIN(...)
#define __API_AVAILABLE_END

#define __API_DEPRECATED(...)
#define __API_DEPRECATED_WITH_REPLACEMENT(...)
#define __API_DEPRECATED_BEGIN(...)
#define __API_DEPRECATED_END

#define __API_UNAVAILABLE(...)
#define __API_UNAVAILABLE_BEGIN(...)
#define __API_UNAVAILABLE_END

/* Legacy macros (still used in some headers) */
#define __OSX_AVAILABLE_STARTING(mac, iphone)
#define __OSX_AVAILABLE_BUT_DEPRECATED(mac_start, mac_dep, iph_start, iph_dep)
#define __OSX_AVAILABLE_BUT_DEPRECATED_MSG(mac_start, mac_dep, iph_start, iph_dep, msg)

/* OS availability macros */
#define __OS_AVAILABILITY(target, availability)
#define __OS_AVAILABILITY_MSG(target, availability, msg)

/* Swift availability (not relevant for C) */
#define __SWIFT_UNAVAILABLE
#define __SWIFT_UNAVAILABLE_MSG(msg)

/* Platform-specific availability macros */
#define __OSX_AVAILABLE(v)
#define __IOS_AVAILABLE(v)
#define __TVOS_AVAILABLE(v)
#define __WATCHOS_AVAILABLE(v)
#define __VISIONOS_AVAILABLE(v)

#define __OSX_DEPRECATED(start, dep, msg)
#define __IOS_DEPRECATED(start, dep, msg)
#define __TVOS_DEPRECATED(start, dep, msg)
#define __WATCHOS_DEPRECATED(start, dep, msg)

#define __OSX_UNAVAILABLE
#define __IOS_UNAVAILABLE
#define __TVOS_UNAVAILABLE
#define __WATCHOS_UNAVAILABLE

/* Platform prohibited macros (used for APIs not available on certain platforms) */
#define __OSX_PROHIBITED
#define __IOS_PROHIBITED
#define __TVOS_PROHIBITED
#define __WATCHOS_PROHIBITED
#define __VISIONOS_PROHIBITED

/* Darwin alias macros (used for symbol versioning) */
#define __DARWIN_ALIAS(x)
#define __DARWIN_ALIAS_C(x)
#define __DARWIN_ALIAS_I(x)
#define __DARWIN_ALIAS_STARTING(x, y, z)
#define __DARWIN_INODE64(x)
#define __DARWIN_1050(x)
#define __DARWIN_EXTSN(x)
#define __DARWIN_EXTSN_C(x)

/* POSIX deprecation macros */
#define __POSIX_C_DEPRECATED(v)

/* Attribute availability (empty) */
#define __AVAILABILITY_INTERNAL__MAC_10_0
#define __AVAILABILITY_INTERNAL__MAC_10_1
#define __AVAILABILITY_INTERNAL__MAC_10_2
#define __AVAILABILITY_INTERNAL__MAC_10_3
#define __AVAILABILITY_INTERNAL__MAC_10_4
#define __AVAILABILITY_INTERNAL__MAC_10_5
#define __AVAILABILITY_INTERNAL__MAC_10_6
#define __AVAILABILITY_INTERNAL__MAC_10_7
#define __AVAILABILITY_INTERNAL__MAC_10_8
#define __AVAILABILITY_INTERNAL__MAC_10_9
#define __AVAILABILITY_INTERNAL__MAC_10_10
#define __AVAILABILITY_INTERNAL__MAC_10_11
#define __AVAILABILITY_INTERNAL__MAC_10_12
#define __AVAILABILITY_INTERNAL__MAC_10_13
#define __AVAILABILITY_INTERNAL__MAC_10_14
#define __AVAILABILITY_INTERNAL__MAC_10_15
#define __AVAILABILITY_INTERNAL__MAC_11_0
#define __AVAILABILITY_INTERNAL__MAC_12_0
#define __AVAILABILITY_INTERNAL__MAC_13_0
#define __AVAILABILITY_INTERNAL__MAC_14_0
#define __AVAILABILITY_INTERNAL__MAC_15_0

/* Include AvailabilityMacros.h for MAC_OS_X_VERSION_* constants */
/* We provide a stub for this too */

#endif /* __AVAILABILITY__ */

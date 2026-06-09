/*
 * CCCC wrapper for sys/cdefs.h
 * 
 * This stub includes Availability.h first (to set up CCCC compatibility macros)
 * then includes the real sys/cdefs.h from the system include path.
 * 
 * The order matters because sys/cdefs.h uses __GNUC__ and __has_attribute
 * to define macros that CCCC cannot parse (like __attribute__((__always_inline__))).
 * By including Availability.h first, we ensure __has_attribute returns 0
 * and __attribute__ is stripped.
 */

#ifndef _CCCC_SYS_CDEFS_H_
#define _CCCC_SYS_CDEFS_H_

/* Include CCCC's Availability.h stub first to set up compatibility macros */
#include <Availability.h>

/* Now include the real sys/cdefs.h - but we need to use include_next */
#include_next <sys/cdefs.h>

#endif /* _CCCC_SYS_CDEFS_H_ */

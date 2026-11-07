/* =============================================================================
 *  Black OS - freestanding primitive types (no libc, no std headers)
 * ========================================================================== */
#ifndef BLACKOS_TYPES_H
#define BLACKOS_TYPES_H

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed char         i8;
typedef signed short        i16;
typedef signed int          i32;
typedef signed long long    i64;
typedef unsigned long long  usize;
typedef long long           isize;

#define NULL            ((void *)0)
#define true            1
#define false           0
#define static_assert(cond, msg)   _Static_assert(cond, msg)

#endif /* BLACKOS_TYPES_H */

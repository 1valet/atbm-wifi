/* User-space stand-ins for the kernel types rx_class.c uses. */
#ifndef RX_CLASS_ENV_H
#define RX_CLASS_ENV_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint16_t __le16;

/* the targets and the test hosts are little-endian */
#define cpu_to_le16(x) ((__le16)(x))
#define le16_to_cpu(x) ((u16)(x))

#endif

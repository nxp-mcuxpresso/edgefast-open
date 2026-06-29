#ifndef MCUX_LIBC_SYS_TIME_WRAPPER_H_
#define MCUX_LIBC_SYS_TIME_WRAPPER_H_

#include <stdint.h>

typedef unsigned int u_int;
typedef int64_t sbintime_t;

/* include the real header after fixing missing typedefs */
#include_next <sys/time.h>

#endif /* MCUX_LIBC_SYS_TIME_WRAPPER_H_ */

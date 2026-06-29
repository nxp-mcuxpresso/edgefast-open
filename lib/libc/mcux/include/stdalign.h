#ifndef _STDALIGN_H_
#define _STDALIGN_H_

/* Minimal stdalign.h fallback for GCC/Clang in freestanding/embedded builds.
 * Provides alignas, alignof, and max_align_t.
 */

#if defined(__cplusplus)
  /* C++11 has built-in alignas/alignof keywords */
  #ifndef alignas
    #define alignas(x) alignas(x)
  #endif
  #ifndef alignof
    #define alignof(x) alignof(x)
  #endif
#else
  /* Prefer C11 keywords if available */
  #if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    #ifndef alignas
      #define alignas(x) _Alignas(x)
    #endif
    #ifndef alignof
      #define alignof(x) _Alignof(x)
    #endif
  #else
    /* GNU extensions fallback */
    #ifndef alignas
      #define alignas(x) __attribute__((aligned(x)))
    #endif
    #ifndef alignof
      #define alignof(x) __alignof__(x)
    #endif
  #endif
#endif

/* max_align_t: use compiler-provided type if available; otherwise approximate.
 * Many embedded newlib builds already provide max_align_t in <stddef.h>.
 */
#include <stddef.h>

#ifndef __max_align_t_defined
  #if defined(__GNUC__) || defined(__clang__)
    typedef struct {
      long long __ll;
      long double __ld;
    } max_align_t;
  #else
    typedef struct {
      long long __ll;
      double __d;
    } max_align_t;
  #endif
  #define __max_align_t_defined 1
#endif

#endif /* _STDALIGN_H_ */

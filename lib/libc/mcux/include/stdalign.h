/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _STDALIGN_H_
#define _STDALIGN_H_

/* In C++, alignas/alignof are keywords; define them only for C. */
#ifndef __cplusplus
#define alignas(x) _Alignas(x)
#define alignof(x) _Alignof(x)
#endif

#endif /* _STDALIGN_H_ */

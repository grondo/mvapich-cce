/* Copyright (c) 2002-2010, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH in the top level MPICH directory.
 *
 */

#ifndef _IBVERBS_HEADER_H
#define _IBVERBS_HEADER_H


#undef IN
#undef OUT

#include <infiniband/verbs.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include "ibverbs_const.h"
#include "via64.h"

double get_us(void);


#define INVAL_HNDL (0xffffffff)

#define IN
#define OUT

#undef MALLOC
#undef FREE
/* src/env/initutil.c NEW not defined */
#define MALLOC(a)    malloc((size_t)(a))
#define CALLOC(a,b)  calloc((size_t)(a),(size_t)(b))
#define FREE(a)      free((char *)(a))
#define NEW(a)    (a *)MALLOC(sizeof(a))
#define STRDUP(a) 	strdup(a)

#if 0
#define D_PRINT(fmt, args...)   {fprintf(stderr, "[%d][%s:%d]", viadev.me, __FILE__, __LINE__);\
                     fprintf(stderr, fmt, ## args); fflush(stderr);}
#else
#define D_PRINT(fmt, args...)
#endif

#define T_PRINT(fmt, args...)

#if __GNUC__ >= 3
# define VIADEV_LIKELY(x)      __builtin_expect (!!(x), 1)
# define VIADEV_UNLIKELY(x)    __builtin_expect (!!(x), 0)
#else
# define VIADEV_LIKELY(x)      (x)
# define VIADEV_UNLIKELY(x)    (x)
#endif

#endif /* _IBVERBS_HEADER_H */

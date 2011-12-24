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
#include <math.h>

#include "ibverbs_const.h"
#include "mv64.h"

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

#define MAX_NUM_HCAS 4

#if 0
#define D_PRINT(fmt, args...)   {fprintf(stderr, "[%d][%s:%d] ", mvdev.me, __FILE__, __LINE__); \
    fprintf(stderr, fmt, ## args); fflush(stderr); }
#else
#define D_PRINT(fmt, args...)
#endif

#if 1
#define MV_ASSERT(x) assert(x)
#else
#define MV_ASSERT(x)
#endif

#if __GNUC__ >= 3
# define MVDEV_LIKELY(x)      __builtin_expect (!!(x), 1)
# define MVDEV_UNLIKELY(x)    __builtin_expect (!!(x), 0)
#else
# define MVDEV_LIKELY(x)      (x)
# define MVDEV_UNLIKELY(x)    (x)
#endif


#endif /* _IBVERBS_HEADER_H */

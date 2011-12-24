/*
 *
 * Copyright (C) 1993 University of Chicago
 * Copyright (C) 1993 Mississippi State University
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Argonne National Laboratory and Mississippi State University as part of MPICH.
 * Modified at Berkeley Lab for MVICH
 *
 */

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

#ifndef MALLOC

/* This file contains the includes for memory operations.  There are two
   forms here.  The first form simply defines MALLOC/FREE/CALLOC as
   their lower-case versions.  The second form, if MPIR_MEMDEBUG is defined,
   uses the tracing allocation library (mpid/util/tr2.c) instead.
 */

#ifdef MPIR_MEMDEBUG
/* tr2.h defines MALLOC/FREE/CALLOC/NEW */
#include "tr2.h"
#define MPIR_trfree MPID_trfree
#define MPIR_trmalloc MPID_trmalloc
/* Make other uses of malloc/free/calloc illegal */
#ifndef malloc
#define malloc $'Use mpimem.h'$
#define free   $'Use mpimem.h'$
#define calloc $'Use mpimem.h'$
#endif
#else
/* We'd like to have a definition for memset etc.  If we can find it ... */
#ifdef STDC_HEADERS
/* Prototype for memset() */
#include <string.h>
#elif defined(HAVE_STRING_H)
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif

/* Need to determine how to declare malloc for all systems, some of which
   may need/allow an explicit declaration */
#include <stdlib.h>
#define MALLOC(a) malloc( (size_t)(a) )
#define FREE   free
#define CALLOC calloc
#define NEW(a) (a *)malloc(sizeof(a))
#endif                          /* MPIR_MEMDEBUG */

#ifndef MEMCPY
#define MEMCPY memcpy
#endif

#endif

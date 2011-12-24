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


#ifndef MPIR_REF_INCR

#if defined(HAVE_MPICHCONF_H) && !defined(MPICHCONF_INC)
/* This includes the definitions found by configure, and can be found in
   the library directory (lib/$ARCH/$COMM) corresponding to this configuration
 */
#define MPICHCONF_INC
#include "mpichconf.h"
#endif

/* 
   This macro file provides an interface for the reference counted objects
   that aids in tracking usage.  If MPIR_OBJDEBUG is not defined, these
   just turn into simple increment and decrement operators (inline).
   With MPIR_OBJDEBUG defined, command-line options may be used to 
   enable tracking the use of reference counted objects.
 */

#ifdef MPIR_OBJDEBUG
#include <stdio.h>

extern FILE *MPIR_Ref_fp;
extern int MPIR_Ref_flags;
#define MPIR_REF_INCR(obj) {                                        \
    (obj)->ref_count++;                                             \
    if (MPIR_Ref_flags) {                                           \
        fprintf( MPIR_Ref_fp,                                       \
                "[%s:%d] incr (to %d) ref count on obj %lx\n",      \
                __FILE__, __LINE__, (obj)->ref_count, (long)obj );  \
    }                                                               \
}

#define MPIR_REF_DECR(obj) {                                        \
    (obj)->ref_count--;                                             \
    if (MPIR_Ref_flags) {                                           \
        fprintf( MPIR_Ref_fp,                                       \
                "[%s:%d] decr (to %d) ref count on obj %lx\n",      \
                __FILE__, __LINE__, (obj)->ref_count, (long)obj);   \
    }                                                               \
}

#define MPIR_REF_MSG(obj,msg) {                                     \
    if (MPIR_Ref_flags) {                                           \
        fprintf( MPIR_Ref_fp,                                       \
                "%s for obj %lx\n", msg, (long)obj );               \
    }                                                               \
}

#define MPIR_REF_SET(obj,val) (obj)->ref_count = val

#else

#define MPIR_REF_INCR(obj) (obj)->ref_count++
#define MPIR_REF_DECR(obj) (obj)->ref_count--
#define MPIR_REF_MSG(obj,msg)
#define MPIR_REF_SET(obj,val) (obj)->ref_count = val

#endif
#endif

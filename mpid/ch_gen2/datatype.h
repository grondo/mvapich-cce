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


#ifndef _DATATYPE_H
#define _DATATYPE_H

/* the following cleans things up so the header file can compile by itself */

#include "cookie.h"             /* needed for MPIR_COOKIE */
#include "mpidefs.h"            /* needed for MPI_Aint */
#include "mpi.h"                /* needed for MPI_Datatype */

#ifndef ANSI_ARGS
#if defined(__STDC__) || defined(__cplusplus) || defined(HAVE_PROTOTYPES)
#define ANSI_ARGS(a) a
#else
#define ANSI_ARGS(a) ()
#endif
#endif

/*****************************************************************************/
/* Datatypes.  The contiguous, predefined datatypes are handled separately   */
/* to demonstrate that the added functionality has low cost                  */
/* In order to conform to MPI 1.1, MPI_Datatype is an int, and is mapped to  */
/* a struct MPIR_DATATYPE * with MPIR_ToPointer.  As an (unimplemented)      */
/* optimziation, the lengths of the predefined datatypes could be held either*/
/* in the ints themselves (e.g., MPI_INT == sizeof(int)) or in an array      */
/* indexed by the values (e.g., datatype_size[datatype] for datatype< 20     */
/*****************************************************************************/

/* Note that MPIR_VECTOR and MPIR_INDEXED are not used - but they'll be needed
   in MPI 2.
 */
typedef enum {
    MPIR_INT, MPIR_FLOAT, MPIR_DOUBLE, MPIR_COMPLEX, MPIR_LONG, MPIR_SHORT,
    MPIR_CHAR, MPIR_BYTE, MPIR_UCHAR, MPIR_USHORT, MPIR_ULONG, MPIR_UINT,
    MPIR_CONTIG, MPIR_VECTOR, MPIR_HVECTOR,
    MPIR_INDEXED,
    MPIR_HINDEXED, MPIR_STRUCT, MPIR_DOUBLE_COMPLEX, MPIR_PACKED,
    MPIR_UB, MPIR_LB, MPIR_LONGDOUBLE, MPIR_LONGLONGINT,
    MPIR_LOGICAL, MPIR_FORT_INT, MPIR_ULONGLONG
} MPIR_NODETYPE;  

#define MPIR_DATATYPE_COOKIE 0xea31beaf
struct MPIR_DATATYPE {
    MPIR_NODETYPE dte_type;     /* type of datatype element this is */
     MPIR_COOKIE                /* Cookie to help detect valid item */
    int committed;              /* whether committed or not */
    int is_contig;              /* whether entirely contiguous */
    int basic;                  /* Is this a basic type */
    int permanent;              /* Is this a permanent type */
    MPI_Aint ub, lb;            /* upper/lower bound of type */
    MPI_Aint real_ub, real_lb;  /* values WITHOUT TYPE_UB/TYPE_LB */
    int has_ub;                 /* Indicates that the datatype has a TYPE_UB */
    int has_lb;                 /* Indicates that the datatype has a TYPE_LB */
    MPI_Aint extent;            /* extent of this datatype */
    int size;                   /* size of type */
    int elements;               /* number of basic elements */
    int ref_count;              /* nodes depending on this node */
    int align;                  /* alignment needed for start of datatype */
    int count;                  /* replication count */
    MPI_Aint stride;            /* stride, for VECTOR and HVECTOR types */
    MPI_Aint *indices;          /* array of indices, for (H)INDEXED, STRUCT */
    int blocklen;               /* blocklen, for VECTOR and HVECTOR types */
    int *blocklens;             /* array of blocklens for (H)INDEXED, STRUCT */
    struct MPIR_DATATYPE *old_type, **old_types, *flattened;
    MPI_Datatype self;          /* Index for this structure */
#ifdef FOO
    MPI_Datatype old_type;      /* type this type is built of, if 1 */
    MPI_Datatype *old_types;    /* array of types, for STRUCT */
    MPI_Datatype flattened;     /* Flattened version, if available */
#endif
};

#define MPIR_GET_DTYPE_PTR(idx)                                     \
(struct MPIR_DATATYPE *)MPIR_ToPointer( idx )

#define MPIR_GET_DTYPE_SIZE(idx,ptr)                                \
    ((ptr)->is_contig) ? (ptr)->size : 0

#define MPIR_TEST_DTYPE(idx,ptr,comm,routine_name) {                \
    if (!(ptr)) {                                                   \
        RETURNV(MPIR_ERROR(comm,                                    \
                    MPIR_ERRCLASS_TO_CODE(MPI_ERR_TYPE,             \
                        MPIR_ERR_TYPE_NULL),routine_name));         \
    }                                                               \
    if ((ptr)->cookie != MPIR_DATATYPE_COOKIE) {                    \
        mpi_errno = MPIR_Err_setmsg(MPI_ERR_TYPE,                   \
                MPIR_ERR_TYPE_CORRUPT,routine_name,                 \
                (char *)0,(char *)0,(ptr)->cookie);                 \
            RETURNV(MPIR_ERROR(comm,mpi_errno,routine_name));       \
    }                                                               \
}

#define MPIR_DATATYPE_ISCONTIG(idx,flag) {                          \
    struct MPIR_DATATYPE *_pp = MPIR_GET_DTYPE_PTR(idx);            \
    *(flag)=(_pp)->is_contig;                                       \
}

/* Used to allocate elements */
extern void *MPIR_dtes;         /* sbcnst datatype elements */

#ifdef NEW_POINTERS

#else

/* Translate between index and datatype pointer */
#define MPIR_GET_REAL_DATATYPE(a) {                                 \
    if(MPIR_TEST_PREDEF_DATATYPE(a)) {                              \
        a = MPIR_datatypes[(MPI_Aint)(a)];                          \
    }                                                               \
}

#define MPIR_TEST_PREDEF_DATATYPE(a)                                \
((MPI_Aint)(a) < (MPI_Aint)MPIR_MAX_DATATYPE_ARRAY                  \
 && (MPI_Aint)(a) > 0)

#define MPIR_DATATYPE_CONTIG(a)                                     \
(MPIR_TEST_PREDEF_DATATYPE(a) || (a)->is_contig)

/* For ONLY the predefined datatypes, 
 * the size MAY be encoded in the value of the datatype */
#define MPIR_DATATYPE_SIZE(a) (1 + ( (MPI_Aint)(a)&0xf ) )

    /* Eventually, this will use MPIR_DATATYPE_SIZE */
#define MPIR_DATATYPE_GET_SIZE(a,contig_size)                       \
    MPIR_GET_REAL_DATATYPE(a);                                      \
    if ((a)->is_contig) {                                           \
        contig_size = (a)->size;                                    \
    } else {                                                        \
        contig_size = 0;                                            \
    }                                                               \
}

#ifdef FOO
#define MPIR_DATATYPE_GET_SIZE(a,contig_size) {                     \
    if (MPIR_TEST_PREDEF_DATATYPE(a)) {                             \
        contig_size=MPIR_DATATYPE_SIZE(a);                          \
    } else {                                                        \
        MPIR_GET_REAL_DATATYPE(a);                                  \
        if ((a)->is_contig) {                                       \
            contig_size = (a)->size;                                \
        } else {                                                    \
            contig_size = 0;                                        \
        }                                                           \
    }                                                               \
}

#endif

#endif                          /* new pointers */

#endif                          /* _DATATYPE_H */

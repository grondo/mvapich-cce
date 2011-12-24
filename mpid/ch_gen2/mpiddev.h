/*
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


/* Definitions for the device only 
   This is an example that can can be used by channel codes
 */
#ifndef MPID_DEV_H
#define MPID_DEV_H


/* 
   Common macro for checking the actual length (msglen) against the
   declared max length in a handle (dmpi_recv_handle).  
   Resets msglen if it is too long; also sets err to MPI_ERR_TRUNCATE.
   This will set the error field to be added to a handle "soon" 
   (Check for truncation)

   This does NOT call the MPID_ErrorHandler because that is for panic
   situations.
 */
#define MPID_CHK_MSGLEN(rhandle,msglen,err)                         \
    if ((rhandle)->len < (msglen)) {                                \
        err = MPI_ERR_TRUNCATE;                                     \
        rhandle->s.MPI_ERROR = MPI_ERR_TRUNCATE;                    \
        msglen = (rhandle)->len;                                    \
    }                                                               

#define MPID_CHK_MSGLEN2(actlen,msglen,err)                         \
    if ((actlen) < (msglen)) {                                      \
        err = MPI_ERR_TRUNCATE;                                     \
        msglen = (actlen);                                          \
    }                                                               \
}

#endif

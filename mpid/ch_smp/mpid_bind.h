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


#ifndef MPID_BIND
#define MPID_BIND
/* These are the bindings of the ADI2 routines 
 * 
 * This is not necessarily a complete set.  Check the ADI2 documentation
 * in http://www.mcs.anl.gov/mpi/mpich/workingnote/nextgen/note.html .
 *
 * These include routines that are internal to the device implementation.
 * A device implementation that replaces a large piece such as the datatype
 * handling may not need to replace all of the routines (e.g., MPID_Msg_rep is 
 * used only by the pack/send/receive routines, not by the MPIR level)
 *
 */

#include "mpid.h"
#include "comm.h"
#include "req.h"

void MPID_Init(int *, char ***, void *, int *);
void MPID_End(void);
void MPID_Abort(struct MPIR_COMMUNICATOR *, int, char *, char *);
int MPID_DeviceCheck ANSI_ARGS((MPID_BLOCKING_TYPE));
void MPID_Node_name ANSI_ARGS((char *, int));
int MPID_WaitForCompleteSend ANSI_ARGS((MPIR_SHANDLE *));
int MPID_WaitForCompleteRecv ANSI_ARGS((MPIR_RHANDLE *));
void MPID_Version_name ANSI_ARGS((char *));

/* SetPktSize is used by util/cmnargs.c */
void MPID_SetPktSize ANSI_ARGS((int));

void MPID_RecvContig
ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int, int, int, int,
           MPI_Status *, int *));
void MPID_IrecvContig
ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int, int, int, int,
           MPI_Request, int *));
void MPID_RecvComplete ANSI_ARGS((MPI_Request, MPI_Status *, int *));
int MPID_RecvIcomplete ANSI_ARGS((MPI_Request, MPI_Status *, int *));

void MPID_SendContig
ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int, int, int, int, int,
           MPID_Msgrep_t, int *));
void MPID_BsendContig
ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int, int, int, int, int,
           MPID_Msgrep_t, int *));
void MPID_SsendContig
ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int, int, int, int, int,
           MPID_Msgrep_t, int *));
void MPID_IsendContig
ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int, int, int, int, int,
           MPID_Msgrep_t, MPI_Request, int *));
void MPID_IssendContig
ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int, int, int, int, int,
           MPID_Msgrep_t, MPI_Request, int *));
void MPID_SendComplete ANSI_ARGS((MPI_Request, int *));
int MPID_SendIcomplete ANSI_ARGS((MPI_Request, int *));

void MPID_Probe
ANSI_ARGS((struct MPIR_COMMUNICATOR *, int, int, int, int *,
           MPI_Status *));
void MPID_Iprobe
ANSI_ARGS((struct MPIR_COMMUNICATOR *, int, int, int, int *, int *,
           MPI_Status *));

void MPID_SendCancel ANSI_ARGS((MPI_Request, int *));
void MPID_RecvCancel ANSI_ARGS((MPI_Request, int *));

/* General MPI Datatype routines  */
void MPID_SendDatatype ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int,
                                  struct MPIR_DATATYPE *,
                                  int, int, int, int, int *));
void MPID_SsendDatatype ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int,
                                   struct MPIR_DATATYPE *,
                                   int, int, int, int, int *));
void MPID_IsendDatatype ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int,
                                   struct MPIR_DATATYPE *,
                                   int, int, int, int, MPI_Request,
                                   int *));
void MPID_IssendDatatype
ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int, struct MPIR_DATATYPE *,
           int, int, int, int, MPI_Request, int *));
void MPID_RecvDatatype
ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int, struct MPIR_DATATYPE *,
           int, int, int, MPI_Status *, int *));
void MPID_IrecvDatatype
ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int, struct MPIR_DATATYPE *,
           int, int, int, MPI_Request, int *));

/* Pack and unpack support */
void MPID_Msg_rep ANSI_ARGS((struct MPIR_COMMUNICATOR *, int,
                             struct MPIR_DATATYPE *,
                             MPID_Msgrep_t *, MPID_Msg_pack_t *));
void MPID_Msg_act ANSI_ARGS((struct MPIR_COMMUNICATOR *, int,
                             struct MPIR_DATATYPE *, MPID_Msgrep_t,
                             MPID_Msg_pack_t *));
void MPID_Pack_size
ANSI_ARGS((int, struct MPIR_DATATYPE *, MPID_Msg_pack_t, int *));
void MPID_Pack
ANSI_ARGS((void *, int, struct MPIR_DATATYPE *, void *, int, int *,
           struct MPIR_COMMUNICATOR *, int, MPID_Msgrep_t, MPID_Msg_pack_t,
           int *));
void MPID_Unpack
ANSI_ARGS((void *, int, MPID_Msgrep_t, int *, void *, int,
           struct MPIR_DATATYPE *, int *, struct MPIR_COMMUNICATOR *, int,
           int *));

/* Requests */
void MPID_Request_free ANSI_ARGS((MPI_Request));

void MPID_Status_set_bytes(MPI_Status * status, int bytes);

/* Communicators 
 * These are often defined as simple macros.  These prototypes show how
 * they should be defined if they are routines.
 */
#define MPID_CommInit(oldcomm,newcomm) MPI_SUCCESS
#define MPID_CommFree(oldcomm) MPI_SUCCESS

#ifndef MPID_CommInit
int MPID_CommInit ANSI_ARGS((struct MPIR_COMMUNICATOR *,
                             struct MPIR_COMMUNICATOR *));
#endif
#ifndef MPID_CommFree
int MPID_CommFree ANSI_ARGS((struct MPIR_COMMUNICATOR *));
#endif

/*
 * Miscellaneous routines
 * These are often defined as simple macros.  These prototypes show how
 * they should be defined if they are routines.
 */
#ifndef MPID_Wtime
void MPID_Wtime ANSI_ARGS((double *));
#endif
#ifndef MPID_Wtick
void MPID_Wtick ANSI_ARGS((double *));
#endif

/* 
 * These are debugging commands; they are exported so that the command-line
 * parser and other routines can control the debugging output
 */
void MPID_SetDebugFile ANSI_ARGS((char *));
void MPID_Set_tracefile ANSI_ARGS((char *));
void MPID_SetSpaceDebugFlag ANSI_ARGS((int));
void MPID_SetDebugFlag ANSI_ARGS((int));
void MPID_SetMsgDebugFlag(int);
#endif

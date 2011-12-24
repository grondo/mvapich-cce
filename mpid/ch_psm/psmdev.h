/*
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Berkeley Lab as part of MVICH.
 *
 * Authors: Bill Saphir      <wcsaphir@lbl.gov>
 *          Michael Welcome  <mlwelcome@lbl.gov>
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

#ifndef _PSMDEV_H
#define _PSMDEV_H

#include "mpid.h"

#define PSMDEV_VERSION "0.9.8"

/* 
 * This file should be included by ADI2 functions. Routines
 * used only internally by the device are in psmpriv.h.
 */

int MPID_PSM_Init(int *argc, char ***argv, int *size, int *rank);

void MPID_PSM_Irecv(void *buf,
                    int len,
                    int src_lrank,
                    int tag,
                    int context_id,
                    MPIR_RHANDLE * rhandle, int *error_code);

void MPID_PSM_RecvComplete(MPIR_RHANDLE *rhandle);

void MPID_PSM_RecvIcomplete(MPIR_RHANDLE *rhandle);

void MPID_PSM_Send(void *buf, int len, int src_lrank, int tag, int context_id,
                   int dest_grank);

void MPID_PSM_Isend(void *buf, int len, int src_lrank, int tag, int context_id,
                    int dest_grank, MPIR_SHANDLE *shandle);

void MPID_PSM_SendComplete(MPIR_SHANDLE *shandle);

int MPID_PSM_SendIcomplete(MPIR_SHANDLE *shandle);

void MPID_PSM_Ssend(void *buf, int len, int src_lrank, int tag, int context_id,
                    int dest_grank);

void MPID_PSM_Issend(void *buf, int len, int src_lrank, int tag, int context_id,
                     int dest_grank, MPIR_SHANDLE *shandle);

void MPID_PSM_Wait(void *psm_req, MPI_Status *status);

void MPID_PSM_Iprobe(int tag, int context_id, int src_lrank, int *found,
                     MPI_Status *status);

void MPID_PSM_Finalize(void);

#endif                          /* _PSMDEV_H */

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


#include "mpid_bind.h"
#include "psmdev.h"
#include "psmparam.h"
#include "psmpriv.h"
#include <unistd.h>


void MPID_SendContig(struct MPIR_COMMUNICATOR *comm_ptr,
                     void *buf,
                     int len,
                     int src_lrank,
                     int tag,
                     int context_id,
                     int dest_grank, MPID_Msgrep_t msgrep, int *error_code)
{
    /*
     * This is the one argument check done in the model adi2 implementation 
     */

    if (buf == 0 && len > 0) {
        *error_code = MPI_ERR_BUFFER;
        return;
    }

  MPID_PSM_Send(buf, len, src_lrank, tag, context_id, dest_grank);
}

void MPID_SsendContig(struct MPIR_COMMUNICATOR *comm_ptr,
                      void *buf,
                      int len,
                      int src_lrank,
                      int tag,
                      int context_id,
                      int dest_grank,
                      MPID_Msgrep_t msgrep, int *error_code)
{
    /*
     * This is the one argument check done in the model adi2 implementation 
     */

    if (buf == 0 && len > 0) {
        *error_code = MPI_ERR_BUFFER;
        return;
    }

  MPID_PSM_Ssend(buf, len, src_lrank, tag, context_id, dest_grank);
}




void MPID_IsendContig(struct MPIR_COMMUNICATOR *comm_ptr,
                      void *buf,
                      int len,
                      int src_lrank,
                      int tag,
                      int context_id,
                      int dest_grank,
                      MPID_Msgrep_t msgrep,
                      MPI_Request request, int *error_code)
{
    /*
     * This is the one argument check done in the model adi2 implementation 
     */

    if (buf == 0 && len > 0) {
        *error_code = MPI_ERR_BUFFER;
        return;
    }

    MPID_PSM_Isend(buf, len, src_lrank, tag, context_id,
                   dest_grank, (MPIR_SHANDLE *)request);
}



void MPID_IssendContig(struct MPIR_COMMUNICATOR *comm_ptr,
                       void *buf,
                       int len,
                       int src_lrank,
                       int tag,
                       int context_id,
                       int dest_grank,
                       MPID_Msgrep_t msgrep,
                       MPI_Request request, int *error_code)
{
    /*
     * This is the one argument check done in the model adi2 implementation 
     * Why isn't it done inside MPI? xxx
     */
    if (buf == 0 && len > 0) {
        *error_code = MPI_ERR_BUFFER;
        return;
    }

    MPID_PSM_Issend(buf, len, src_lrank, tag, context_id,
                    dest_grank, (MPIR_SHANDLE *)request);
}


void MPID_SendComplete(MPI_Request request, int *error_code)
{
    MPIR_SHANDLE *shandle = (MPIR_SHANDLE *) request;
    while (!shandle->is_complete)
        MPID_DeviceCheck(MPID_BLOCKING);
}

/* Does not ensure any communication progress*/
int MPID_SendIcomplete(MPI_Request request, int *error_code)
{
    MPIR_SHANDLE *shandle = (MPIR_SHANDLE *) request;
    if (shandle->is_complete)
        return 1;

    return MPID_PSM_SendIcomplete(shandle);
}

void MPID_SendCancel(MPI_Request r, int *error_code)
{
    error_abort_all(GEN_EXIT_ERR, "MPID_SendCancel not implemented");
}


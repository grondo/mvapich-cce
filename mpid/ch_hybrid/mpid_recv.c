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
#include "mv_dev.h"
#include "queue.h"
#include "reqalloc.h"
#include "mv_priv.h"

void MPID_RecvContig(struct MPIR_COMMUNICATOR *comm_ptr,
                     void *buf,
                     int len,
                     int src_lrank,
                     int tag,
                     int context_id, MPI_Status * status, int *error_code)
{


    /* Ok to allocate on stack because this is a blocking call
     * Allocated rhandles are automatically initialized, but here we 
     * need to call RecvInit.
     */

    MPIR_RHANDLE rhandle;
    MPID_RecvInit(&rhandle);

    MPID_MV_Irecv(buf, len, src_lrank, tag, context_id,
                   &rhandle, error_code);

    if (!*error_code) {
        while (!rhandle.is_complete)
            MPID_DeviceCheck(MPID_BLOCKING);
    }

    if (status)
        *status = rhandle.s;
}


void MPID_IrecvContig(struct MPIR_COMMUNICATOR *comm_ptr,
                      void *buf,
                      int len,
                      int src_lrank,
                      int tag,
                      int context_id, MPI_Request request, int *error_code)
{

    /* Do we need to initialize request here? No. 
     * Upper levels have allocated the request and set the 
     * reference count. 
     */

    /* do arg checking in VIA_Irecv() */
    MPID_MV_Irecv(buf, len, src_lrank, tag, context_id,
                   (MPIR_RHANDLE *) request, error_code);

}


void MPID_RecvComplete(MPI_Request request,
                       MPI_Status * status, int *error_code)
{
    MPIR_RHANDLE *rhandle = (MPIR_RHANDLE *) request;

    while (!rhandle->is_complete) {
        MPID_DeviceCheck(MPID_BLOCKING);
    }

    if (status) {
        *status = rhandle->s;
    }

    *error_code = rhandle->s.MPI_ERROR;
}

int MPID_RecvIcomplete(MPI_Request request,
                       MPI_Status * status, int *error_code)
{
    MPIR_RHANDLE *rhandle = (MPIR_RHANDLE *) request;
    MPID_DeviceCheck(MPID_NOTBLOCKING);
    if (rhandle->is_complete) {
        if (status)
            *status = rhandle->s;
        *error_code = rhandle->s.MPI_ERROR;
        return 1;
    }
    return 0;
}

void MPID_RecvCancel(MPI_Request r, int *error_code)
{
    MPIR_RHANDLE *rhandle = (MPIR_RHANDLE *) r;

    /* check state, if packet has arrived, must allow to continue
     * otherwise can cancel.
     */

    if (rhandle->can_cancel) {
        /* remove from posted queue */
        MPID_QUEUE *queue = &MPID_recvs.posted;

        *error_code = MPID_Dequeue(queue, rhandle);
        if (*error_code != MPI_SUCCESS) {
            return;
        }

        /* set the status flag to indicate msg was cancelled */
        rhandle->s.MPI_TAG = MPIR_MSG_CANCELLED;

        /* mark the request as complete, must be reaped by
         * MPID_REQUEST_FREE, MPI_WAIT or MPI_TEST
         */
        rhandle->is_complete = 1;

        /* cant cancel again */
        rhandle->can_cancel = 0;
    }

    *error_code = MPI_SUCCESS;
}

/* Temp fix for MPI_Status_set_elements, needed in Romio */
void MPID_Status_set_bytes(MPI_Status * status, int bytes)
{
    status->count = bytes;
}

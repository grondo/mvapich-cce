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
#include "reqalloc.h"
#include "psmpriv.h"


void MPID_RecvContig(struct MPIR_COMMUNICATOR *comm_ptr,
                     void *buf,
                     int len,
                     int src_lrank,
                     int tag,
                     int context_id, MPI_Status * status, int *error_code)
{
    /* Allocated rhandles are automatically initialized, but here we 
     * need to call RecvInit.
     */

    MPIR_RHANDLE rhandle;
    MPID_RecvInit(&rhandle);

    MPID_PSM_Irecv(buf, len, src_lrank, tag, context_id,
                   &rhandle, error_code);

    if (!*error_code) {
      MPID_PSM_Wait(&(rhandle.req), status);
    }
}


void MPID_IrecvContig(struct MPIR_COMMUNICATOR *comm_ptr,
                      void *buf,
                      int len,
                      int src_lrank,
                      int tag,
                      int context_id, MPI_Request request, int *error_code)
{
  MPID_PSM_Irecv(buf, len, src_lrank, tag, context_id,
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

  if(!rhandle->is_complete)
    MPID_PSM_RecvIcomplete(rhandle);

  if(rhandle->is_complete)
  {
    if(status)
      *status = rhandle->s;

    *error_code = rhandle->s.MPI_ERROR;
    return 1;
  }

  return 0;
}

void MPID_RecvCancel(MPI_Request r, int *error_code)
{
    psm_error_t retVal;
    MPIR_RHANDLE *rhandle = (MPIR_RHANDLE *)r;

    /* check state, if packet has arrived, must allow to continue
     * otherwise can cancel.
     */
    if (rhandle->can_cancel) {
        /* remove from posted queue */
        retVal = psm_mq_cancel(&(rhandle->req));

        if(retVal == PSM_MQ_NO_COMPLETIONS)
        {
          *error_code = MPI_SUCCESS; 
          return;
        }
        else if(retVal == PSM_OK)
        { 
          /*Returning the request storage to the MQ library */
           psm_mq_test(&(rhandle->req), NULL);

          /* set the status flag to indicate msg was cancelled */
          rhandle->s.MPI_TAG = MPIR_MSG_CANCELLED;

          /* mark the request as complete, must be reaped by
           * MPID_REQUEST_FREE, MPI_WAIT or MPI_TEST
           */
          rhandle->is_complete = 1;

          /* cant cancel again */
          rhandle->can_cancel = 0;

          *error_code = MPI_SUCCESS;
          return;
        }
    }

    *error_code = MPI_ERR_BUFFER;/*TODO: Set the proper error*/
}

/* Temp fix for MPI_Status_set_elements, needed in Romio */
void MPID_Status_set_bytes(MPI_Status * status, int bytes)
{
    status->count = bytes;
}

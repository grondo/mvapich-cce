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


#include <unistd.h>
#include "mpid.h"
#include "mpiddev.h"
#include "reqalloc.h"
#include "queue.h"

void MPID_Request_free(MPI_Request request)
{
    int mpi_errno = MPI_SUCCESS;

    if (request == NULL)
        return;

    /* rely on the fact that SendIcomplete and RecvIcomplete
     * do a device check
     */
    switch (request->handle_type) {
    case MPIR_SEND:
        if (MPID_SendIcomplete(request, &mpi_errno)) {
            MPID_SendFree(request);
            request = 0;
        }
        break;
    case MPIR_RECV:
        if (MPID_RecvIcomplete(request, (MPI_Status *) 0, &mpi_errno)) {
            MPID_RecvFree(request);
            request = 0;
        }
        break;
    case MPIR_PERSISTENT_SEND:
        if (MPID_SendIcomplete(request, &mpi_errno)) {
            MPID_PSendFree(request);
            request = 0;
        }
        break;
    case MPIR_PERSISTENT_RECV:
        if (MPID_RecvIcomplete(request, (MPI_Status *) 0, &mpi_errno)) {
            MPID_PRecvFree(request);
            request = 0;
        }
        break;
    default:
        MPID_Abort((struct MPIR_COMMUNICATOR *) 0, 1, (char *)"MPI internal",
                   (char *)"Unknown request type in MPID_Request_free");
        break;
    }


    /* the device will still have pointers (I hope!) to 
     * pending sends and receives. They will be freed
     * automatically when the request completes
     * and the device notices the zero reference count. 
     * See SEND_COMPLETE and RECV_COMPLETE in viapriv.h
     */

    if (request != NULL) {
        request->chandle.ref_count--;
    }
}




void MPID_Node_name(char *name, int namelen)
{
    gethostname(name, namelen);
}



void MPID_Iprobe(struct MPIR_COMMUNICATOR *comm_ptr,
                 int tag,
                 int context_id,
                 int src_lrank,
                 int *found, int *error_code, MPI_Status * status)
{
    MPIR_RHANDLE *rhandle;

    /* Model implementation searches unexpected queue before even
     * checking the device. Since the VIA device does not
     * make progress except when we check it, this first search
     * through the queue will probably fail for real applications. 
     * Also, checking the device is very quick if there is nothing
     * there. So check the device first. 
     */


    MPID_DeviceCheck(MPID_NOTBLOCKING);
    MPID_Search_unexpected_queue(src_lrank, tag, context_id, 0, &rhandle);

    if (rhandle) {
        *found = 1;
        *status = rhandle->s;
    } else {
        *found = 0;
    }
}

void MPID_Probe(struct MPIR_COMMUNICATOR *comm_ptr,
                int tag,
                int context_id,
                int src_lrank, int *error_code, MPI_Status * status)
{
    int found;

    *error_code = 0;
    while (1) {
        /* since MPID_Iprobe always makes progress, no reason for 
         * us to do a CheckDevice here
         */
        MPID_Iprobe(comm_ptr, tag, context_id, src_lrank, &found,
                    error_code, status);
        if (found || *error_code)
            break;
    }
}

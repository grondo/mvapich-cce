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


/*
 * Hacked version of adi2hsend.c from sample implementation
 */

#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"
#include "mpid_pack.h"
#include "viapriv.h"

static int MPID_IsendDatatypeFinish(void *handle)
{
    void *buf;
    buf = ((MPIR_SHANDLE *) handle)->local_address;
    if (buf != NULL) {
        ((MPIR_SHANDLE *)handle)->local_address = NULL;
        FREE(buf);
    }
    ((MPIR_SHANDLE *)handle)->finish = NULL;
    return (0);
}

void MPID_SendDatatype(struct MPIR_COMMUNICATOR *comm_ptr,
                       void *buf,
                       int count,
                       struct MPIR_DATATYPE *dtype_ptr,
                       int src_lrank,
                       int tag,
                       int context_id, int dest_grank, int *error_code)
{
    int len, contig_size;
    void *mybuf;
    MPID_Msgrep_t msgrep = MPID_MSGREP_RECEIVER;
    MPID_Msg_pack_t msgact = MPID_MSG_OK;

    /*
     * Alogrithm:
     * If datatype is contiguous, just send it. 
     * Else create a local buffer, use SendContig, and then free the buffer.
     */
    contig_size = MPIR_GET_DTYPE_SIZE(datatype, dtype_ptr);

    if (contig_size > 0 || count == 0) {
        if (Is_MPI_Bottom(buf, count, dtype_ptr)) {
            buf = MPID_Adjust_Bottom(buf, dtype_ptr);
        }
        len = contig_size * count;
        MPID_SendContig(comm_ptr, buf, len, src_lrank, tag, context_id,
                        dest_grank, msgrep, error_code);
        return;
    }

    mybuf = 0;
    MPID_PackMessage(buf, count, dtype_ptr, comm_ptr, dest_grank, msgrep,
                     msgact, (void **) &mybuf, &len, error_code);
    if (*error_code)
        return;


    MPID_SendContig(comm_ptr, mybuf, len, src_lrank, tag, context_id,
                    dest_grank, msgrep, error_code);

    /* mybuf might be NULL if message length is zero */
    if (mybuf)
        FREE(mybuf);

}

/*
 * Noncontiguous datatype isend
 * This is a simple implementation.  Note that in the rendezvous case, the
 * "pack" could be deferred until the "ok to send" message arrives.  To
 * implement this, the individual "send" routines would have to know how to
 * handle general datatypes.  
 */
void MPID_IsendDatatype(struct MPIR_COMMUNICATOR *comm_ptr,
                        void *buf,
                        int count,
                        struct MPIR_DATATYPE *dtype_ptr,
                        int src_lrank,
                        int tag,
                        int context_id,
                        int dest_grank,
                        MPI_Request request, int *error_code)
{
    int len, contig_size;
    void *mybuf;
    MPID_Msgrep_t msgrep = MPID_MSGREP_RECEIVER;
    MPID_Msg_pack_t msgact = MPID_MSG_OK;

    request->shandle.finish = NULL;

    contig_size = MPIR_GET_DTYPE_SIZE(datatype, dtype_ptr);
    if (contig_size > 0 || count == 0) {
        if (Is_MPI_Bottom(buf, count, dtype_ptr)) {
            buf = MPID_Adjust_Bottom(buf, dtype_ptr);
        }
        len = contig_size * count;
        MPID_IsendContig(comm_ptr, buf, len, src_lrank, tag, context_id,
                         dest_grank, msgrep, request, error_code);

        if((&viadev.connections[dest_grank])->ext_sendq_size >= 
                viadev_progress_threshold) {
            MPID_DeviceCheck(MPID_NOTBLOCKING);
        }

        return;
    }

    mybuf = 0;
    MPID_PackMessage(buf, count, dtype_ptr, comm_ptr, dest_grank,
                     msgrep, msgact, (void **) &mybuf, &len, error_code);
    if (*error_code)
        return;

    request->shandle.finish = MPID_IsendDatatypeFinish;

    request->shandle.local_address = mybuf;

    MPID_IsendContig(comm_ptr, mybuf, len, src_lrank, tag, context_id,
                     dest_grank, msgrep, request, error_code);

    if((&viadev.connections[dest_grank])->ext_sendq_size >= 
            viadev_progress_threshold) {
        MPID_DeviceCheck(MPID_NOTBLOCKING);
    }

    /* Note that, from the users perspective, the message is now complete
       (!) since the data is out of the input buffer (!) */
}

void MPID_IssendDatatype(struct MPIR_COMMUNICATOR *comm_ptr,
                         void *buf,
                         int count,
                         struct MPIR_DATATYPE *dtype_ptr,
                         int src_lrank,
                         int tag,
                         int context_id,
                         int dest_grank,
                         MPI_Request request, int *error_code)
{
    int len, contig_size;
    void *mybuf;
    MPID_Msgrep_t msgrep = MPID_MSGREP_RECEIVER;
    MPID_Msg_pack_t msgact = MPID_MSG_OK;

    request->shandle.finish = NULL;

    contig_size = MPIR_GET_DTYPE_SIZE(datatype, dtype_ptr);
    if (contig_size > 0 || count == 0) {
        if (Is_MPI_Bottom(buf, count, dtype_ptr)) {
            buf = MPID_Adjust_Bottom(buf, dtype_ptr);
        }
        len = contig_size * count;
        MPID_IssendContig(comm_ptr, buf, len, src_lrank, tag, context_id,
                          dest_grank, msgrep, request, error_code);
        return;
    }

    mybuf = 0;
    MPID_PackMessage(buf, count, dtype_ptr, comm_ptr, dest_grank,
                     msgrep, msgact, (void **) &mybuf, &len, error_code);
    if (*error_code)
        return;

    request->shandle.finish = MPID_IsendDatatypeFinish;

    request->shandle.local_address = mybuf;

    MPID_IssendContig(comm_ptr, mybuf, len, src_lrank, tag, context_id,
                      dest_grank, msgrep, request, error_code);

    /* Note that, from the users perspective, the message is now complete
       (!) since the data is out of the input buffer (!) */
}

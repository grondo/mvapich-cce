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
#include "viadev.h"
#include "viapriv.h"
#include "queue.h"
#include "viaparam.h"
#include "mpid_smpi.h"

void MPID_VIA_Irecv(void *buf, int len, int src_lrank, int tag,
                    int context_id, MPIR_RHANDLE * rhandle,
                    int *error_code)
{
    MPIR_RHANDLE *unexpected;

    /* assume success, change if necessary */
    *error_code = MPI_SUCCESS;

    /* error check. Both MPID_Recv and MPID_Irecv go through this code */
    if ((NULL == buf) && (len > 0)) {
        *error_code = MPI_ERR_BUFFER;
        return;
    }
    /* this is where we would register the buffer and get a dreg entry */
    /*rhandle->bytes_as_contig = len; changed for totalview */
    rhandle->len = len;
    /*rhandle->start = buf; changed for totalview support */
    rhandle->buf = buf;
    rhandle->dreg_entry = 0;
    rhandle->is_complete = 0;
    rhandle->replied = 0;
    rhandle->bytes_copied_to_user = 0;
    rhandle->vbufs_received = 0;
    rhandle->protocol = VIADEV_PROTOCOL_RENDEZVOUS_UNSPECIFIED;
    rhandle->can_cancel = 1;

    /* At this point we don't fill in:
     * vbufs_expected, vbufs_received, bytes_received, remote_address, 
     * remote_memory_handle. connection, vbuf_head, vbuf_tail.
     */

    MPID_Search_unexpected_queue_and_post(src_lrank,
                                          tag, context_id, rhandle,
                                          &unexpected);

    if (unexpected) {
#ifdef _SMP_
        if(!disable_shared_mem) {
            if ((VIADEV_PROTOCOL_SMP_SHORT == unexpected->protocol) && (NULL != unexpected->connection)) {
                MPID_SMP_Eagerb_unxrecv_start_short(rhandle, unexpected);
                return;
            }
#ifdef _SMP_RNDV_
            if ((VIADEV_PROTOCOL_SMP_RNDV == unexpected->protocol) && (NULL != unexpected->connection)){
                MPID_SMP_Rndvn_unxrecv_posted(rhandle, unexpected);
                return;
            }
#endif
        }
#endif

        /* special case for self-communication */
        if (NULL == unexpected->connection) {
            MPID_VIA_self_finish(rhandle, unexpected);
            MPID_RecvFree(unexpected);
        } 
    }
}


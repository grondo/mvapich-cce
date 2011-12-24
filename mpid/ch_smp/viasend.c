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

#include "via64.h"
#include "mpid.h"
#include "viadev.h"
#include "viapriv.h"
#include "viapacket.h"
#include "queue.h"
#include "viaparam.h"

int MPID_VIA_self_start(void *buf, int len, int src_lrank, int tag,
                        int context_id, MPIR_SHANDLE * shandle)
{

    MPIR_RHANDLE *rhandle;
    int rc = MPI_SUCCESS;
    int found;

    /* receive has been posted and is on the posted queue */
    MPID_Msg_arrived(src_lrank, tag, context_id, &rhandle, &found);

    if (found) {
        memcpy(rhandle->buf, buf, len);
        rhandle->s.MPI_TAG = tag;
        rhandle->s.MPI_SOURCE = src_lrank;
        RECV_COMPLETE(rhandle);

        rhandle->s.count = len;

        SEND_COMPLETE(shandle);
        /* need to call the finish routine for the receive? */
        return rc;
    }

    /* receive handle has been created 
     * and posted on unexpected queue */
    rhandle->connection = NULL;

    if (len < viadev_rendezvous_threshold) {
        /* copy the buffer and mark the send complete so there is a bit of
         * buffering for silly programs that send-to-self blocking.  Hide
         * info on the send in vbuf_{head,tail} which we know will not
         * be used.  */
        rhandle->send_id = 0;
        rhandle->s.count = len;
        rhandle->vbuf_head = malloc(len);
        rhandle->vbuf_tail = (void *) ((aint_t) len);
        memcpy(rhandle->vbuf_head, buf, len);
        SEND_COMPLETE(shandle);
    } else {
        rhandle->send_id = REQ_TO_ID(shandle);
        rhandle->s.count = len;
        shandle->local_address = buf;
        shandle->is_complete = 0;
        shandle->bytes_total = len;
    }

    return rc;
}

void MPID_VIA_self_finish(MPIR_RHANDLE * rhandle,
                          MPIR_RHANDLE * unexpected)
{
    MPIR_SHANDLE *shandle;

    void *send_ptr;
    int bytes_total;
    int truncate = 0;


    shandle = (MPIR_SHANDLE *) ID_TO_REQ(unexpected->send_id);

    if (shandle) {
        send_ptr = shandle->local_address;
        bytes_total = shandle->bytes_total;
    } else {
        send_ptr = unexpected->vbuf_head;
        bytes_total = (aint_t) unexpected->vbuf_tail;
    }

    if (bytes_total > rhandle->len)
        /* user error */
        truncate = 1;
    else
        /* fewer or same bytes were sent as were specified in the recv */
        rhandle->len = bytes_total;

    memcpy(rhandle->buf, send_ptr, rhandle->len);

    /* this is similate to copy_unexpected_handle_to_user_handle, but
     * we need many fewer fields 
     */
    rhandle->s.MPI_TAG = unexpected->s.MPI_TAG;
    rhandle->s.MPI_SOURCE = unexpected->s.MPI_SOURCE;
    rhandle->connection = NULL;
    RECV_COMPLETE(rhandle);

    if (truncate)
        rhandle->s.MPI_ERROR = MPI_ERR_TRUNCATE;

    if (shandle) {
        SEND_COMPLETE(shandle);
    } else {
        free(unexpected->vbuf_head);
    }
}


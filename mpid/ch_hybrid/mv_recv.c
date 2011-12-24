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
#include "mv_priv.h"
#include "mv_inline.h"
#include "queue.h"
#include "mv_buf.h"
#include "mv_param.h"
#include "mpid_smpi.h"
#include "dreg.h"

void MPID_MV_Irecv(void *buf, int len, int src_lrank, int tag,
                    int context_id, MPIR_RHANDLE * rhandle,
                    int *error_code)
{
    MPIR_RHANDLE *unexpected;

    D_PRINT("Posting recv. len: %d, lrank: %d, tag: %d, context: %d\n",
            len, src_lrank, tag, context_id);

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
    rhandle->is_complete = 0;
    rhandle->replied = 0;
    rhandle->bytes_copied_to_user = 0;
    rhandle->vbufs_received = 0;
    rhandle->protocol = MVDEV_PROTOCOL_RENDEZVOUS_UNSPECIFIED;
    rhandle->can_cancel = 1;
    rhandle->dreg_entry = NULL;

    /* At this point we don't fill in:
     * vbufs_expected, vbufs_received, bytes_received, remote_address, 
     * remote_memory_handle. connection, vbuf_head, vbuf_tail.
     */

    MPID_Search_unexpected_queue_and_post(src_lrank,
                                          tag, context_id, rhandle,
                                          &unexpected);

    if (unexpected) {
        D_PRINT("found in unexpected!\n");
#ifdef _SMP_
        if(!disable_shared_mem) {
            if ((MVDEV_PROTOCOL_SMP_SHORT == unexpected->protocol) && (NULL != unexpected->connection)) {
                MPID_SMP_Eagerb_unxrecv_start_short(rhandle, unexpected);
                return;
            }
#ifdef _SMP_RNDV_
            if ((MVDEV_PROTOCOL_SMP_RNDV == unexpected->protocol) && (NULL != unexpected->connection)){
                MPID_SMP_Rndvn_unxrecv_posted(rhandle, unexpected);
                return;
            }
#endif
        }
#endif

        /* special case for self-communication */
        if (NULL == unexpected->connection) {
            MPID_MV_self_finish(rhandle, unexpected);
            MPID_RecvFree(unexpected);
        } else {
            mvdev_copy_unexpected_handle_to_user_handle(rhandle,
                                                         unexpected,
                                                         error_code);
            MPID_RecvFree(unexpected);

            if (*error_code != MPI_SUCCESS) {
                return;
            }

            /* packets of matching send have arrived, can no longer
             * cancel the recv */
            rhandle->can_cancel = 0;

            /* sender spedified a protocol, which one? */
            switch (rhandle->protocol) {
                case MVDEV_PROTOCOL_R3:
                    /* MV_Rndv_Receive_Start_RPut(rhandle); */
                    mvdev_recv_r3(rhandle); 
                    break;
                case MVDEV_PROTOCOL_EAGER:
                    mvdev_eager_pull(rhandle);
                    break;
                case MVDEV_PROTOCOL_UD_ZCOPY:
                    mvdev_recv_ud_zcopy(rhandle);
                    break;
                case MVDEV_PROTOCOL_UNREL_RPUT:
                case MVDEV_PROTOCOL_REL_RPUT:
                    MV_Rndv_Receive_Start_RPut(rhandle);
                    /* mvdev_recv_rc_rput(rhandle); */
                    break;
                default:
                    error_abort_all(GEN_EXIT_ERR,
                            "MPID_MV_Irecv: unknown protocol %d\n",
                            rhandle->protocol);
            }

        }
    } else {
        D_PRINT("recv now posted to queue\n");
    }
}

/*
 * Make progress on a receive. This is invoked when we post
 * a receive that matches an eager entry in the unexpected queue. 
 * In this case we need to copy data that has come in so far. 
 * After this initial batch of vbufs, subsequent packets will be  
 * processed as they come in. 
 */

void mvdev_eager_pull(MPIR_RHANDLE * rhandle)
{
    mv_buf *v;

    MV_ASSERT(rhandle->protocol == MVDEV_PROTOCOL_EAGER);
    MV_ASSERT(rhandle->vbuf_head != NULL);

    while(NULL != (v = (mv_buf *) rhandle->vbuf_head)) {
        if(v->byte_len) {
            memcpy(((char *) rhandle->buf) + rhandle->bytes_copied_to_user,
                    v->ptr, v->byte_len);
        }
        rhandle->bytes_copied_to_user += v->byte_len;
        rhandle->vbuf_head = v->next_ptr;

        MV_ASSERT(rhandle->bytes_copied_to_user <= rhandle->len);

        release_mv_buf(v);
    }

    rhandle->vbuf_tail = NULL;

    if(rhandle->bytes_copied_to_user == rhandle->len) {
        RECV_COMPLETE(rhandle);
        D_PRINT("recv of from eager_pull is complete\n");
    } else {
        D_PRINT("not all are recv'ed, %d of %d\n", rhandle->bytes_copied_to_user, rhandle->len);
    }
}

void mvdev_copy_unexpected_handle_to_user_handle(
        MPIR_RHANDLE * rhandle,
        MPIR_RHANDLE * unexpected,
        int *error_code)
{

    rhandle->s.MPI_SOURCE = unexpected->s.MPI_SOURCE;
    rhandle->s.MPI_TAG = unexpected->s.MPI_TAG;
    rhandle->vbufs_received = unexpected->vbufs_received;

    /* changes here if RDMA is allowed */

    rhandle->connection = unexpected->connection;
    rhandle->vbuf_head = unexpected->vbuf_head;
    rhandle->vbuf_tail = unexpected->vbuf_tail;
    rhandle->send_id = unexpected->send_id;
    rhandle->protocol = unexpected->protocol;

    /* check for truncation */
    if (unexpected->len > rhandle->len) {
        *error_code = MPI_ERR_COUNT;
        return; 
        /*        error(ERR_FATAL, "message truncated. ax %d got %d", 
                  rhandle->len, 
                  unexpected->len);
                  */
    } else {
        rhandle->s.count = unexpected->len;
        rhandle->len = unexpected->len;
    }

    /* if this is an eager send currently being transferred, 
     * connection handle may have a link to us
     */
    if (((mvdev_connection_t *) 
                (rhandle->connection))->rhandle == unexpected) {
        ((mvdev_connection_t *) (rhandle->connection))->rhandle = rhandle;
    }
}


void mvdev_recv_r3(MPIR_RHANDLE * rhandle)
{
    MV_Rndv_Send_Reply(rhandle);
}




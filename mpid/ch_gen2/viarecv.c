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
#include "vbuf.h"
#include "dreg.h"
#include "viaparam.h"
#include "mpid_smpi.h"
#include "sig_handler.h"
#include "nfr.h"

#ifdef MCST_SUPPORT
#include "bcast_info.h"
extern bcast_info_t *bcast_info;
void process_acks(void);
void slide_window(void);
void check_time_out(void);
void MPID_RC_Send(void *, int, int, int, int, int, int *);
void viadev_rendezvous_cancel(MPIR_RHANDLE *);
void MPID_VIA_mcst_recv(void *, int, int, MPIR_RHANDLE *, int *);
extern int retransmission;
#endif

void MPID_VIA_Irecv(void *buf, int len, int src_lrank, int tag,
                    int context_id, MPIR_RHANDLE * rhandle,
                    int *error_code)
{
    MPIR_RHANDLE *unexpected;

    /* assume success, change if necessary */
    *error_code = MPI_SUCCESS;

    if (viadev_async_progress) {
        MPID_DeviceCheck(MPID_NOTBLOCKING);
    }

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

    if (viadev_use_nfr) {
        rhandle->fin = NULL;
        rhandle->next = NULL;
        rhandle->prev = NULL;
        rhandle->was_retransmitted = 0;
    }

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
        } else {
            viadev_copy_unexpected_handle_to_user_handle(rhandle,
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
            case VIADEV_PROTOCOL_R3:
                {
                    viadev_recv_r3(rhandle);
                    break;
                }
            case VIADEV_PROTOCOL_RPUT:
                {
                    viadev_recv_rput(rhandle);
                    break;
                }
            case VIADEV_PROTOCOL_RGET:
                {
                    viadev_recv_rget(rhandle);
                    break;
                }
            case VIADEV_PROTOCOL_RENDEZVOUS_UNSPECIFIED:
                {
                    /* should always be specified on sender side */
                    error_abort_all(GEN_EXIT_ERR, "invalid protocol:"
                                        "RENDEZVOUS_UNSPECIFIED");
                    break;
                }
            case VIADEV_PROTOCOL_EAGER:
                {
                    rhandle->vbufs_expected =
                        viadev_calculate_vbufs_expected
                        (rhandle->len, rhandle->protocol);
                    viadev_eager_pull(rhandle);
                    break;
                }
            case VIADEV_PROTOCOL_EAGER_COALESCE:
                {
                    viadev_eager_coalesce_pull(rhandle);
                    break;
                }
            default:
                {
                    error_abort_all(GEN_EXIT_ERR,
                                        "MPID_VIA_Irecv: unknown protocol %d\n",
                                        rhandle->protocol);
                }
            }

        }
    }
    else {
        /* async protocol: Posting an irecv so enable signal handler */
        updateSigAction(1);
    }
}

void viadev_recv_r3(MPIR_RHANDLE * rhandle)
{
    rhandle->vbufs_expected =
        viadev_calculate_vbufs_expected(rhandle->len,
                                        rhandle->protocol);
    if(viadev_use_srq) {
        pthread_spin_lock(&viadev.srq_post_spin_lock);

        if(viadev.posted_bufs <= viadev_credit_preserve) {

            viadev.posted_bufs += 
                viadev_post_srq_buffers(rhandle->vbufs_expected);
        }

        pthread_spin_unlock(&viadev.srq_post_spin_lock);

    } else {

        viadev_prepost_for_rendezvous((viadev_connection_t *) rhandle->
                connection, rhandle->vbufs_expected);
    }

    viadev_rendezvous_reply(rhandle);
}

void viadev_recv_rget(MPIR_RHANDLE * rhandle)
{
    vbuf *v;
    viadev_packet_rget *rget_packet = NULL;

    viadev_connection_t *c = 
        (viadev_connection_t *) rhandle->connection;

    /* sender has specified RDMA READ, can we match it? */

    /* first, if receive buffer is NULL, must be a zero length
     * transfer, just use a dummy buffer location so that we have
     * something to transfer */
    if(NULL == rhandle->buf) {

        rhandle->buf = &nullrbuffer;
    }

    /* Remote address is NULL, cannot perform RDMA Read */
    if(NULL == rhandle->remote_address) {
        error_abort_all(IBV_STATUS_ERR,
                "RDMA read with null remote buffer not supported, rhandle : %p\n",
                rhandle);
    }

	/* Trac #239. */
    if(viadev_use_nfr) {
        if (VIADEV_LIKELY(0 == rhandle->was_retransmitted)) {
            rhandle->dreg_entry = NULL;
            viadev_register_recvbuf_if_possible(rhandle);
        }
    } else {
        rhandle->dreg_entry = NULL;
        viadev_register_recvbuf_if_possible(rhandle);
    }
    
    if(NULL == rhandle->dreg_entry) {
        /* Sender specified RDMA read, but we are unable
         * to register the receive buffer, should have
         * an explicit message to sender to let him know of
         * this situation */
        error_abort_all(IBV_STATUS_ERR,
                "Registration of recvbuf failed - RDMA read failed\n");
    }

    /* ok, managed to register memory */


    /* Get vbuf for RDMA Read and issue it */
    v = get_vbuf();

    assert(v != NULL);

    rget_packet = (viadev_packet_rget *) VBUF_BUFFER_START(v);

    /* This field is set in the vbuf data area to allow us
     * to later find on RDMA read completion, which receive
     * request it corresponded to
     */

    rget_packet->rreq = rhandle;

    if (viadev_use_nfr && 0 == rhandle->was_retransmitted) {
        assert(rhandle->prev == NULL && rhandle->next ==NULL);
        NFR_RNDV_ADD(&(c->rndv_inprocess), rhandle);
    }

    V_PRINT(DEBUG03, "RDMA read on id %d key %d\n", rhandle->sn, rhandle->remote_memhandle_rkey);
    viadev_rget(c, v, (void *)rhandle->buf,
            ((struct dreg_entry *) rhandle->dreg_entry)->memhandle->rkey,
            (void*) rhandle->remote_address,
            rhandle->remote_memhandle_rkey, 
            rhandle->len);
}

void viadev_rget(viadev_connection_t * c, vbuf * v, void *local_address,
                 uint32_t local_memhandle, void *remote_address,
                 uint32_t remote_memhandle, int nbytes)
{
    void *buffer = VBUF_BUFFER_START(v);
    viadev_packet_rget *h = (viadev_packet_rget *)buffer;
    V_PRINT(DEBUG03, "viadev_rget: RDMA read\n");

    vbuf_init_rget(v, local_address, local_memhandle,
                   remote_address, remote_memhandle, nbytes);
    h->header.type = VIADEV_PACKET_RGET;
    viadev_post_rdmaread(c, v);
}


void viadev_recv_rput(MPIR_RHANDLE * rhandle)
{
    /* only way buffer is NULL is if length is zero */
    if (NULL == rhandle->buf) {
        rhandle->buf = &nullrbuffer;
    }
    viadev_register_recvbuf_if_possible(rhandle);
    if (NULL == rhandle->dreg_entry) {
        /* failed to register memory, revert to R3 */
        rhandle->protocol = VIADEV_PROTOCOL_R3;
        viadev_recv_r3(rhandle);
        return;
    }
    /* ok, managed to register memory */
    rhandle->vbufs_expected =
        viadev_calculate_vbufs_expected(rhandle->len,
                                        rhandle->protocol);
    viadev_rendezvous_reply(rhandle);
}

void viadev_register_recvbuf_if_possible(MPIR_RHANDLE * rhandle)
{
    /* note that dreg_register() will return NULL if it fails */
    rhandle->dreg_entry = dreg_register(rhandle->buf,
                                        rhandle->len,
                                        DREG_ACL_WRITE);
}

/* 
 * Copy a system-generated receive request from the unexpected queue
 * to a user request after finding a match.  
 *
 * After if search_unexpected_and_post finds a match, we now we have
 * two rhandles - the real one passed to us and and the placeholder
 * one on the unexpected queue, which has minimal information.
 *
 * This routine moves information from the placeholder handle into the
 * real handle, dequeues the queued handle and makes progress on the
 * receive if possible.  (for model, see chnrndv.c->Rndvn_unxrecv_start)
 *
 * xxx This is a lot of data to copy, but it all seems
 * necessary. 
 *      
 * xxx we could do sanity checks here. 
 *  
 */

void viadev_copy_unexpected_handle_to_user_handle(MPIR_RHANDLE * rhandle,
                                                  MPIR_RHANDLE *
                                                  unexpected,
                                                  int *error_code)
{

    rhandle->s.MPI_SOURCE = unexpected->s.MPI_SOURCE;
    rhandle->s.MPI_TAG = unexpected->s.MPI_TAG;
    rhandle->vbufs_received = unexpected->vbufs_received;
    rhandle->bytes_sent = 0;
    rhandle->remote_address = unexpected->remote_address;
    rhandle->remote_memhandle_rkey = unexpected->remote_memhandle_rkey;
    rhandle->connection = unexpected->connection;
    rhandle->vbuf_head = unexpected->vbuf_head;
    rhandle->vbuf_tail = unexpected->vbuf_tail;
    rhandle->send_id = unexpected->send_id;
    rhandle->protocol = unexpected->protocol;
    rhandle->coalesce_data_buf = unexpected->coalesce_data_buf;

    if(viadev_use_nfr) {
        rhandle->sn = unexpected->sn;
    }

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
    if (((viadev_connection_t *) (rhandle->connection))->rhandle
        == unexpected) {
        ((viadev_connection_t *) (rhandle->connection))->rhandle = rhandle;
    }
}

/*
 * calculate_vbufs_expected is called whenever we get
 * a match between a posted receive and an incoming packet. 
 * This happens either when a receive is posted and matches
 * an entry on the unexpected queue or when a packet
 * comes in that matches a pre-posted receive. 
 * 
 * In general, it is not until this match that we have information
 * from both sides that allows us to figure out how many packets
 * will be sent to complete the transfer. 
 * 
 * When the transfer is through a rendezvous protocol, the
 * specific type of rendezvous will have been determined before
 * this routine is called. 
 *
 */


int viadev_calculate_vbufs_expected(int nbytes, viadev_protocol_t protocol)
{
    int first_packet, next_packet;
    switch (protocol) {
    case VIADEV_PROTOCOL_EAGER:
        {
            first_packet = VBUF_BUFFER_SIZE -
                sizeof(viadev_packet_eager_start);
            next_packet = VBUF_BUFFER_SIZE -
                sizeof(viadev_packet_eager_next);
            break;
        }
    case VIADEV_PROTOCOL_R3:
        {
            first_packet = VBUF_BUFFER_SIZE -
                sizeof(viadev_packet_r3_data);
            next_packet = first_packet;
            break;
        }
    case VIADEV_PROTOCOL_RPUT:
        {
            /* this is number of rputs by sender.
             * receiver gets only one packet */
            first_packet = viadev.maxtransfersize;
            next_packet = viadev.maxtransfersize;
            break;
        }
    default:
        {
            /* return to MPI */
            first_packet = 1;
            next_packet = 1;
            error_abort_all(GEN_EXIT_ERR,
                                "calculate_vbufs_expected: unexpected protocol %d",
                                protocol);
        }
    }

    nbytes -= first_packet;

    if (nbytes > 0) {
        return (2 + (nbytes - 1) / next_packet);
    } else {
        return (1);
    }
}


#ifdef MCST_SUPPORT

void viadev_rendezvous_cancel(MPIR_RHANDLE * rhandle)
{
    vbuf *v;
    viadev_packet_rendezvous_reply *packet;
    viadev_connection_t *c = (viadev_connection_t *) rhandle->connection;
    int rdma_ok;

    co_print("Sending rendezvous cancel message\n");

#if defined(RDMA_FAST_PATH)
    rdma_ok = fast_rdma_ok(c, sizeof(viadev_packet_rendezvous_reply),0);

    if (rdma_ok) {
        v = &(c->RDMA_send_buf[c->phead_RDMA_send]);
        packet = (viadev_packet_rendezvous_reply *) v->buffer;
    } else {
        v = get_vbuf();
        packet = (viadev_packet_rendezvous_reply *) VBUF_BUFFER_START(v);
    }
#else
    v = get_vbuf();
    packet = (viadev_packet_rendezvous_reply *) VBUF_BUFFER_START(v);
#endif

    PACKET_SET_HEADER(packet,
                      ((viadev_connection_t *) rhandle->connection),
                      VIADEV_PACKET_RENDEZVOUS_CANCEL);

    assert(rhandle->send_id != NULL);
    packet->sreq = rhandle->send_id;
    packet->rreq = REQ_TO_ID(rhandle);
    packet->protocol = rhandle->protocol;

    D_PRINT("VI %3d R3 OKTS len %d",
            ((viadev_connection_t *) rhandle->connection)->global_rank,
            rhandle->len);

#if defined(RDMA_FAST_PATH)
    if (rdma_ok) {
        post_fast_rdma_with_completion(c,
                                       sizeof
                                       (viadev_packet_rendezvous_reply));
    } else {
        vbuf_init_send(v, sizeof(viadev_packet_rendezvous_reply));
        viadev_post_send(((viadev_connection_t *) rhandle->connection), v);
    }
#else
    vbuf_init_send(v, sizeof(viadev_packet_rendezvous_reply));
    viadev_post_send(((viadev_connection_t *) rhandle->connection), v);
#endif

    MPID_RecvFree(rhandle);
}

#endif

/*
 * reply for rendezvous protocol 
 * This routine is called as soon as a match is made 
 */

void viadev_rendezvous_reply(MPIR_RHANDLE * rhandle)
{
    vbuf *v;
    viadev_packet_rendezvous_reply *packet;

    if (rhandle->replied) {
        error_abort_all(GEN_EXIT_ERR,
                            "Extra rendezvous reply attempted.");
        return;
    }
    if (rhandle->protocol != VIADEV_PROTOCOL_R3 &&
        rhandle->protocol != VIADEV_PROTOCOL_RPUT &&
        rhandle->protocol != VIADEV_PROTOCOL_RGET) {
        error_abort_all(GEN_EXIT_ERR,
                            "Inappropriate protocol %d for rendezvous reply\n",
                            rhandle->protocol);
    }

    v = get_vbuf();
    packet = (viadev_packet_rendezvous_reply *) VBUF_BUFFER_START(v);

    PACKET_SET_HEADER_NFR(packet,
                        ((viadev_connection_t *) rhandle->connection),
                        VIADEV_PACKET_RENDEZVOUS_REPLY);

    if (rhandle->dreg_entry != NULL) {
        packet->memhandle_rkey =
            ((dreg_entry *) rhandle->dreg_entry)->memhandle->rkey;
        packet->buffer_address = rhandle->buf;
    } else {
        packet->memhandle_rkey = 0;
        packet->buffer_address = NULL;
    }

    packet->sreq = rhandle->send_id;
    packet->rreq = REQ_TO_ID(rhandle);
    packet->protocol = rhandle->protocol;

    D_PRINT("VI %3d R3 OKTS len %d",
            ((viadev_connection_t *) rhandle->connection)->global_rank,
            rhandle->len);

    vbuf_init_send(v, sizeof(viadev_packet_rendezvous_reply));
    viadev_post_send(((viadev_connection_t *) rhandle->connection), v);

    rhandle->replied = 1;
}


/*
 * Make progress on a receive. This is invoked when we post
 * a receive that matches an eager entry in the unexpected queue. 
 * In this case we need to copy data that has come in so far. 
 * After this initial batch of vbufs, subsequent packets will be  
 * processed as they come in. 
 */

void viadev_eager_pull(MPIR_RHANDLE * rhandle)
{
    viadev_packet_eager_start *start;
    viadev_packet_eager_next *next;
    char *vbuf_data;
    viadev_connection_t *c = (viadev_connection_t *) rhandle->connection;
    vbuf *v;
    assert(rhandle->protocol == VIADEV_PROTOCOL_EAGER);

    /* we have just posted a receive that matches an in-progress
     * eager send. There is a chain of vbufs on the rhandle. First
     * vbuf is an EAGER_START, others are EAGER_NEXT
     */

    assert(rhandle->vbuf_head != NULL);

    v = (vbuf *) (rhandle->vbuf_head);
    start = (viadev_packet_eager_start *) VBUF_BUFFER_START(v);

#if !defined(DISABLE_HEADER_CACHING) && \
    defined(ADAPTIVE_RDMA_FAST_PATH)
    if (start->header.type == FAST_EAGER_CACHED) {
        vbuf_data = (char *) (start) + FAST_EAGER_HEADER_SIZE;
        c = &(viadev.connections[v->grank]);

        if (start->header.fast_eager_size != 0) {
            memcpy(rhandle->buf, vbuf_data,
                   start->header.fast_eager_size);
        }
        rhandle->bytes_copied_to_user = start->header.fast_eager_size;

        rhandle->vbuf_head = v->desc.next;
    } else {
        vbuf_data = (char *) (start) + sizeof(viadev_packet_eager_start);
        c = &(viadev.connections[v->grank]);

        if (start->bytes_in_this_packet != 0) {
            memcpy(rhandle->buf, vbuf_data, start->bytes_in_this_packet);
        }
        rhandle->bytes_copied_to_user = start->bytes_in_this_packet;

        rhandle->vbuf_head = v->desc.next;
    }

#else
    vbuf_data = (char *) (start) + sizeof(viadev_packet_eager_start);

    c = &(viadev.connections[v->grank]);

    if (start->bytes_in_this_packet != 0) {
        memcpy(rhandle->buf, vbuf_data, start->bytes_in_this_packet);
    }
    rhandle->bytes_copied_to_user = start->bytes_in_this_packet;
    rhandle->vbuf_head = v->desc.next;
#endif

#ifdef ADAPTIVE_RDMA_FAST_PATH
    if (v->padding == NORMAL_VBUF_FLAG)
        release_vbuf(v);
    else
        release_recv_rdma(c, v);
#else
    release_vbuf(v);
#endif

    while (rhandle->vbuf_head != NULL) {
        /* set up pointers */
        v = rhandle->vbuf_head;
        next = (viadev_packet_eager_next *) VBUF_BUFFER_START(v);
        vbuf_data = (char *) (next) + sizeof(viadev_packet_eager_next);

        assert(next->header.type == VIADEV_PACKET_EAGER_NEXT);

        memcpy((char *) (rhandle->buf) + rhandle->bytes_copied_to_user,
               vbuf_data, next->bytes_in_this_packet);

        rhandle->bytes_copied_to_user += next->bytes_in_this_packet;
        /* pop the just-copied vbuf off the list */

        rhandle->vbuf_head = v->desc.next;

#if defined(ADAPTIVE_RDMA_FAST_PATH)
        if (v->padding == NORMAL_VBUF_FLAG)
            release_vbuf(v);
        else
            release_recv_rdma(c, v);
#else
        release_vbuf(v);
#endif
    }
    rhandle->vbuf_head = NULL;
    rhandle->vbuf_tail = NULL;

    if (rhandle->vbufs_expected == rhandle->vbufs_received) {
        assert(rhandle->bytes_copied_to_user == rhandle->len);
        RECV_COMPLETE(rhandle);
    }
}


void viadev_eager_coalesce_pull(MPIR_RHANDLE * rhandle)
{
    char *vbuf_data;
    viadev_connection_t *c = (viadev_connection_t *) rhandle->connection;           vbuf *v;

    assert(rhandle->vbuf_head != NULL);

    v = (vbuf *) (rhandle->vbuf_head);

    vbuf_data = rhandle->coalesce_data_buf;
    c = &(viadev.connections[v->grank]);

    D_PRINT("before copy, target: %p, vbuf_data: %p, len: %d\n",
            rhandle->buf, vbuf_data, rhandle->len);

    if (rhandle->len != 0) {
        memcpy(rhandle->buf, vbuf_data, rhandle->len);
    }

    rhandle->bytes_copied_to_user = rhandle->len;
    v->ref_count--;

    if(v->ref_count == 0) {
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (v->padding == NORMAL_VBUF_FLAG)
            release_vbuf(v);
        else
            release_recv_rdma(c, v);
#else
        release_vbuf(v);
#endif
    }

    rhandle->vbuf_head = NULL;
    rhandle->vbuf_tail = NULL;

    RECV_COMPLETE(rhandle);
}


/* When a rendezvous connection is in progress, we like to have
 * more than the default number of preposted vbufs available, so
 * that the sender can proceed without blocking 
 */

void viadev_prepost_for_rendezvous(viadev_connection_t * c,
                                   int vbufs_expected)
{

    int needed;
    /* extra preposts we want is c->rendezvous_packets_expected but
     * we are limited to VIADEV_PREPOST_RENDEZVOUS_EXTRA.
     * change RENDEZVOUS_EXTRA to a total PREPOST_MAX
     */

    /* record total number of vbufs expected for Rendzvous data receives */
    c->rendezvous_packets_expected += vbufs_expected;

    /* figure number of preposts needed to satisfy current demand, but
     * limit so that we don't prepost tons of vbufs.  */

    needed = c->rendezvous_packets_expected;
    if (needed > viadev_prepost_rendezvous_extra) {
        needed = viadev_prepost_rendezvous_extra;
    }
    needed += viadev_prepost_depth + viadev_prepost_noop_extra;

    assert(c->initialized);
    while (c->preposts < (int) viadev_rq_size && needed > c->preposts) {
        PREPOST_VBUF_RECV(c);
    }
    viadev_send_noop_ifneeded(c);
}

#ifdef MCST_SUPPORT

void viadev_mcst_pull(MPIR_RHANDLE *);
void remote_ack_write(int, int, int);

void MPID_VIA_mcst_recv(void *buf,
                        int len,
                        int root, MPIR_RHANDLE * rhandle, int *error_code)
{
    void *databuf;
    int tag, co_index, cur_bcnt;
    int mask, co_rank=0, relative_rank=0, size=0, src;
#ifndef DIRECT_COROOT
    int dst, Dest;
#endif
    int parent, buf_head;
    int startmax = VBUF_DATA_SIZE(viadev_packet_eager_start);
    int whom;
    MPIR_RHANDLE *unexpected;
    vbuf *v;
    viadev_packet_eager_start *h;

    bcast_info->bcast_called = 1;

    if (buf == NULL && len > 0) {
        *error_code = MPI_ERR_BUFFER;
        return;
    }

    cur_bcnt = bcast_info->Bcnt[root];

    /* "rank" of the co-root */
    co_index = CORANK(root);

    /* Checking for co-root cond. */
    if ((LEFT_OVERS == 0) ||((root <= LAST_NODE) && (viadev.me <= LAST_NODE))){
    mask = 0x1;
    if (viadev.me % NUM_CHILD == root % NUM_CHILD) {

        co_rank = CORANK(viadev.me);
        size = NUM_COROOTS;
        relative_rank = (co_rank >= CORANK(root)) ?
            co_rank - CORANK(root) : co_rank - CORANK(root) + size;

        while (mask < size) {
            if (relative_rank & mask) {
                src = co_rank - mask;
                if (src < 0)
                    src += size;
                break;
            }
            mask <<= 1;
        }

        parent = CORANK_TO_RANK(src);

        /*Process the acks */
        process_acks();

        /*Slide the window */
        slide_window();

        /* Check for time out */
        check_time_out();

    }
    }

    /* this is where we would register the buffer and get a dreg entry */
    rhandle->len = len;
    rhandle->buf = buf;
    rhandle->is_complete = 0;
    rhandle->replied = 0;
    rhandle->bytes_copied_to_user = 0;
    rhandle->vbufs_received = 0;
    rhandle->protocol = VIADEV_PROTOCOL_RENDEZVOUS_UNSPECIFIED;
    rhandle->can_cancel = 1;
    rhandle->dreg_entry = 0;

    co_print("Searching in Unexp Q,src:%d,tag:%d,cnxt:-1\n", root,
             cur_bcnt);
    MPID_Search_unexpected_queue_and_post(root, cur_bcnt, -1, rhandle,
                                          &unexpected);

    if (unexpected) {

        co_print("Unexpected\n");
        co_print("Unexpected messg:Protocol:%d\n", rhandle->protocol);

        if (unexpected->protocol == VIADEV_PROTOCOL_UD_MCST) {

            /*copy the stuff into the user buffer */
            rhandle->vbufs_received = unexpected->vbufs_received;
            rhandle->vbuf_head = unexpected->vbuf_head;
            rhandle->vbuf_tail = unexpected->vbuf_tail;
            rhandle->protocol = unexpected->protocol;

        } else {
            viadev_copy_unexpected_handle_to_user_handle(rhandle,
                                                         unexpected,
                                                         error_code);
        }
        MPID_RecvFree(unexpected);

        /* packets of matching send have arrived, can no longer
         * cancel the recv */
        rhandle->can_cancel = 0;

        /*      We are handling all these cases because the co-roots are 
         *      receiving rc messages
         * */
        co_print("Unexpected messg:Protocol:%d\n", rhandle->protocol);

        switch (rhandle->protocol) {
        case VIADEV_PROTOCOL_UD_MCST:
            co_print("pulling mcst pkt\n");
            viadev_mcst_pull(rhandle);
            ++(bcast_info->Bcnt[root]);
            break;
        case VIADEV_PROTOCOL_R3:
            viadev_recv_r3(rhandle);
            break;
        case VIADEV_PROTOCOL_RPUT:
            viadev_recv_rput(rhandle);
            break;
        case VIADEV_PROTOCOL_RGET:
            viadev_recv_rget(rhandle);
            break;
        case VIADEV_PROTOCOL_EAGER:
            rhandle->vbufs_expected =
                viadev_calculate_vbufs_expected(rhandle->len,
                                                rhandle->protocol);
            co_print("pulling eager pkt\n");
            viadev_eager_pull(rhandle);
            ++(bcast_info->Bcnt[root]);
            break;
        default:
            {
                error_abort_all(GEN_EXIT_ERR,
                                    "MPID_VIA_mrecv: unknown protocol %d\n",
                                    rhandle->protocol);
            }

        }
    } else {
        co_print("Expected\n");
    }

    /* No unexpected messages..Poll for device check */
    while (!(rhandle->is_complete))
        MPID_DeviceCheck(MPID_NOTBLOCKING);


    if ((viadev.me % NUM_CHILD == root % NUM_CHILD) &&
       ((LEFT_OVERS==0) || ((root <= LAST_NODE) && (viadev.me <= LAST_NODE)))) {    

#ifndef DIRECT_COROOT
        /* Relaying the message to the other coroots */
        mask >>= 1;
        while (mask > 0) {
            if (relative_rank + mask < size) {
                dst = co_rank + mask;
                if (dst >= size)
                    dst -= size;
                Dest = CORANK_TO_RANK(dst);
                co_print("Dest:%d\n", Dest);
                MPID_RC_Send(buf, len, root, cur_bcnt, -1, Dest,
                             error_code);
            }
            mask >>= 1;
        }
#endif

        /* Storing the message in the buffer */
        if (bcast_info->Bcnt[root] >= bcast_info->win_head[co_index]) {

            /* index of the co-root */
            co_index = CORANK(root);

            if (bcast_info->is_full[co_index] != 0) {
                co_print
                    ("Spinning at the coroot till the buffer is free\n");
                while (viadev.bcast_info.is_full[co_index] == 1) {
                    process_acks();
                    slide_window();
                    check_time_out();
                }

            }

            co_print("Storing the message from the root\n");
            co_print("src:%d,tag:%d,len:%d\n", root, cur_bcnt, len);
            buf_head = bcast_info->buf_head[co_index];
            v = &(bcast_info->co_buf[co_index][buf_head]);

            /* Setting up the packet, making it easier for retransmission */

            h = (viadev_packet_eager_start *) (v);
            databuf = ((char *) h) + sizeof(viadev_packet_eager_start);

            tag = cur_bcnt;

            PACKET_SET_ENVELOPE(h, -1, tag, len, root);
            (h->header).type = VIADEV_PACKET_UD_MCST;
            (h->envelope).data_length = len;
            h->bytes_in_this_packet = len;
            if (startmax < h->bytes_in_this_packet)
                h->bytes_in_this_packet = startmax;

            if (h->bytes_in_this_packet != 0)
                memcpy(databuf, buf, h->bytes_in_this_packet);


            bcast_info->time_out[co_index][buf_head] =
                get_us() + BCAST_TIME_OUT;

            bcast_info->buf_head[co_index] =
                (++bcast_info->buf_head[co_index]) % SENDER_WINDOW;

            if (bcast_info->buf_head[co_index] ==
                bcast_info->buf_tail[co_index])
                viadev.bcast_info.is_full[co_index] = 1;
            else
                viadev.bcast_info.is_full[co_index] = 0;

            bcast_info->win_head[co_index] =
                ++(bcast_info->win_head[co_index]);

            co_print("Finished copying message\n");

        }

    } else {
        /*Write the ack to the root if I am not co-root */
        if ((LEFT_OVERS == 0)) {
            whom = viadev.me - (viadev.me % NUM_CHILD) + (root % NUM_CHILD);
            co_index = CORANK(root);
        }
        else {
            if (viadev.me > LAST_NODE){
                if (root > LAST_NODE){
                    whom = root;
                    co_index = 0;
                }
                else{
                    whom = 0;
                    co_index = CORANK(root);
                    co_print("I am here\n");
                }
            }
            else{
                if ((root > LAST_NODE)){
                    whom = root;
                    co_index = 0;
                }
                else{
                    whom = viadev.me - (viadev.me % NUM_CHILD) + (root % NUM_CHILD);
                    co_index = CORANK(root);
                }
            }
        }
        
        while (bcast_info->ack_full == 1) {
            retransmission = 1;
            MPID_DeviceCheck(MPID_NOTBLOCKING);
            retransmission = 0;
        }

        if ((LEFT_OVERS ==0) || (whom != viadev.me))
        remote_ack_write(whom, cur_bcnt, co_index);

    }

    bcast_info->Bcnt[root] = cur_bcnt + 1;
    co_print("Finished Bcast:%d\n", cur_bcnt);

}

void viadev_mcst_pull(MPIR_RHANDLE * rhandle)
{

    viadev_packet_eager_start *start;
    char *vbuf_data;
    vbuf *v;

    assert(rhandle->protocol == VIADEV_PROTOCOL_UD_MCST);

    assert(rhandle->vbuf_head != NULL);

    v = (vbuf *) (rhandle->vbuf_head);
    start = (viadev_packet_eager_start *) (VBUF_BUFFER_START(v) + 40);
    vbuf_data = (char *) (start) + sizeof(viadev_packet_eager_start);


    if (start->bytes_in_this_packet != 0)
        memcpy(rhandle->buf, vbuf_data, start->bytes_in_this_packet);

    rhandle->bytes_copied_to_user = start->bytes_in_this_packet;

    release_vbuf(v);

    RECV_COMPLETE(rhandle);
}

/* co_root: the actual rank of the coroot, co_index:index of the coroot*/

void remote_ack_write(int co_root, int Bcnt, int co_index)
{

    int src_lrank, child_rank, index;
    int *remote_co_ack_buf;
    int *ack_buf;
    int *remote_address;
    struct ibv_sge sg_entry_s;
    struct ibv_send_wr sr, *bad_wr;
    viadev_connection_t *c;

    child_rank = CHILD(viadev.me);
    co_print("Acking co_root:%d,co_index:%d,Bcnt:%d\n", co_root, co_index,
             Bcnt);
    co_print("Child_rank:%d\n", child_rank);

    remote_address = (int *) (bcast_info->remote_add[co_root]);

    if (LEFT_OVERS == 0){
    co_print("remote_address:%p\n", remote_address);
    remote_co_ack_buf = &remote_address[co_index * NUM_CHILD];

    /* Local Ack_buffer for transmitting Acks */
    ack_buf = (int *) ((char *) viadev.bcast_info.ack_buffer +
                       ACK_LEN * NUM_CHILD * NUM_COROOTS +
                       ACK_LEN * NUM_COROOTS);
    }
    else{
        /* Acking rank zero node ? */
        if (co_root == 0){
            remote_co_ack_buf =
                &remote_address[co_index * (NUM_CHILD + LEFT_OVERS)];
        }
        else {
            if (co_root > LAST_NODE)
                remote_co_ack_buf = remote_address;
            else{
                remote_co_ack_buf = &remote_address[co_index * NUM_CHILD];
            }
        }

        if (viadev.me == 0){
            ack_buf = (int *) ((char *) viadev.bcast_info.ack_buffer +
                    ACK_LEN * (LEFT_OVERS+NUM_CHILD) * NUM_COROOTS +
                    ACK_LEN * NUM_COROOTS);
        }
        else {
            if (viadev.me > LAST_NODE){
                ack_buf = (int *) ((char *) viadev.bcast_info.ack_buffer +
                        ACK_LEN * (viadev.np) + ACK_LEN * 1);
            }
            else{
                ack_buf = (int *) ((char *) viadev.bcast_info.ack_buffer +
                        ACK_LEN * NUM_CHILD * NUM_COROOTS +
                        ACK_LEN * NUM_COROOTS);
            }
        }

    }

    index = bcast_info->sbuf_head;
    ack_buf[index] = Bcnt;
    co_print("index:%d\n", index);
/*
    viadev.bcast_info.sbuf_head = (++viadev.bcast_info.sbuf_head) %
	NUM_WBUFFERS;
*/

    viadev.bcast_info.sbuf_head = (++viadev.bcast_info.sbuf_head) %
        MAX_ACKS;

    if (viadev.bcast_info.sbuf_head == viadev.bcast_info.sbuf_tail) {
        viadev.bcast_info.ack_full = 1;
    } else {
        viadev.bcast_info.ack_full = 0;
    }

    src_lrank = co_root;
    c = &viadev.connections[src_lrank];

    memset (&sr, 0, sizeof (sr));
    sr.send_flags = IBV_SEND_SIGNALED;
    sr.opcode = IBV_WR_RDMA_WRITE;
    sr.wr_id = -1;
    sr.wr.rdma.remote_addr = (uintptr_t)(&remote_co_ack_buf[child_rank]);
     

    if (LEFT_OVERS == 0){
        sr.wr.rdma.remote_addr = (uintptr_t) (&remote_co_ack_buf[child_rank]);
    }
    else {
        if (src_lrank == 0){
            if (viadev.me <= LAST_NODE){
                sr.wr.rdma.remote_addr = (uintptr_t) (&remote_co_ack_buf[child_rank]);
            }
            else{
                sr.wr.rdma.remote_addr = (uintptr_t) (&remote_co_ack_buf[LAST_RANK]);
            }
        }
        else{
            if (src_lrank > LAST_NODE){
                sr.wr.rdma.remote_addr = (uintptr_t) (&remote_co_ack_buf[viadev.me]);
            }
            else{
                sr.wr.rdma.remote_addr = (uintptr_t) (&remote_co_ack_buf[child_rank]);
            }
        }
    }
    
    sr.imm_data = -20;
    sr.num_sge = 1;
    sr.wr.rdma.rkey = bcast_info->remote_rkey[src_lrank];
    sg_entry_s.length = ACK_LEN;
    sg_entry_s.lkey = bcast_info->ack_mem_hndl->lkey;
    sg_entry_s.addr = (uintptr_t)(&ack_buf[index]);
    sr.sg_list = &sg_entry_s;

    XRC_FILL_SRQN(sr, c->global_rank);
    
    if (ibv_post_send (c->vi, &sr, &bad_wr)){
        printf("ibv_post_send failed\n");
    }
    co_print("remote_key:%d\n", sr.r_key);

}

#endif

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

#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include "viadev.h"
#include "viaparam.h"
#include "viapriv.h"
#include "vbuf.h"
#include "dreg.h"
#include "mpid_smpi.h"

#include "cm.h"
#include "cm_user.h"

void odu_enable_qp(int peer, struct ibv_qp * qp)
{
    int i = peer, j;

    viadev_connection_t *c = &viadev.connections[i];

    assert(i!=viadev.me);
    
    c->global_rank = i;
#ifdef XRC
    if (!viadev_use_xrc)
#endif 
    {
        c->next_packet_expected = 1;
        c->next_packet_tosend = 1;
    }
    c->shandle_head = NULL;
    c->shandle_tail = NULL;
    c->rhandle = NULL;
    c->nextflow = NULL;
    c->inflow = 0;
    c->preposts = 0;
    c->send_wqes_avail = viadev_sq_size;
    c->ext_sendq_size = 0;
    c->ext_sendq_head = NULL;
    c->ext_sendq_tail = NULL;
    c->rendezvous_packets_expected = 0;
    viadev_init_backlog_queue(c);

    c->coalesce_cached_out.context = -1;
    c->coalesce_cached_in.context = -1;

    c->rdma_reads_avail = viadev_default_qp_ous_rd_atom;
    c->ext_rdma_read_head = NULL;
    c->ext_rdma_read_tail = NULL;

#if defined(ADAPTIVE_RDMA_FAST_PATH)
#ifndef DISABLE_HEADER_CACHING
    /* init cached packet header */
    memset(&(c->cached_outgoing), 0,
            sizeof(viadev_packet_eager_start));
    memset(&(c->cached_incoming), 0,
            sizeof(viadev_packet_eager_start));
#ifdef VIADEV_DEBUG
    c->cached_hit = c->cached_miss = 0;
#endif
    c->cached_incoming.header.type = c->cached_outgoing.header.type
        = FAST_EAGER_CACHED;
#endif
#endif

    if (viadev_use_nfr) {
        WAITING_LIST_INIT(&c->waiting_for_ack);
        WAITING_LIST_INIT(&c->rndv_inprocess);
        c->pending_acks = 0;
        c->progress_recov_mode = 0;
        c->in_restart = 0;
    }

    c->qp_status = QP_UP;

    /* If I already have (an indirect) connection, continue to use it until
     * the new QP is active */
    if (NULL == c->vi)
        c->vi = viadev.qp_hndl[i];

    if(!viadev_use_srq) {
        /* prepost receives */
        for (j = 0; j < viadev_initial_prepost_depth; j++) {
            PREPOST_VBUF_RECV(c);
        }
    }

    c->remote_credit = viadev_initial_credits;
    c->remote_cc = viadev_initial_credits;
    c->local_credit = 0;
    c->preposts = viadev_initial_prepost_depth;

    c->pending_r3_data = 0;
    c->received_r3_data = 0;

    if(viadev_use_srq) {
        c->initialized = 1;
    } else {
        c->initialized = (viadev_initial_prepost_depth
                ==
                viadev_prepost_depth +
                viadev_prepost_noop_extra);
    }

#if defined(ADAPTIVE_RDMA_FAST_PATH)
    /* initialize revelant fields */
    c->rdma_credit = 0;
    c->num_no_completion = 0;
    /* By default, all pointers are cleared, indicating no buffers
     * at the receive and no credits at the sender side. Only if 
     * RDMA eager buffers are allocated, this pointers will be updated
     */
    c->phead_RDMA_send = 0;
    c->ptail_RDMA_send = 0;
    c->p_RDMA_recv = 0;
    c->p_RDMA_recv_tail = 0;
    /* remote address and handle not available yet */
    c->remote_address_received = 0;
#endif
#ifdef XRC 
    {
        xrc_queue_t *iter = viadev.req_queue, *next;
        
        if (c->xrc_flags & XRC_FLAG_START_RDMAFP) {
            c->xrc_flags ^= XRC_FLAG_START_RDMAFP;
            vbuf_fast_rdma_alloc (c, 1);
            vbuf_rdma_address_send (c);
        }

        while (iter) {
            next = iter->next;
            if (iter->src_rank == c->global_rank) {
                vbuf *v = iter->v;
                viadev_packet_header *header = 
                    (viadev_packet_header *) VBUF_BUFFER_START(v);
            
                if (iter->prev) 
                    iter->prev->next = iter->next;
                else
                    viadev.req_queue = iter->next;

                if (iter->next)
                    iter->next->prev = iter->prev;

                free (iter);
                viadev_incoming_rendezvous_start (v, c, 
                        (viadev_packet_rendezvous_start *) header);

            }
            iter = next;
        }
    }
#endif
}

void odu_re_enable_qp(int peer)
{
    int ret;
    viadev_connection_t *c = &(viadev.connections[peer]);

    CM_DBG("NR re-enable qp on peer %d\n",peer);
    assert(c->global_rank != viadev.me);
    /* close old qp, copy new one */
    if (0 == c->in_restart) {
        ret = ibv_destroy_qp(c->vi);
        if (0 != ret) {
            CM_ERR("CM could not destroy QP: %s[%d][%d]",strerror(errno), errno, ret);
        }
    } else {
        c->in_restart = 0;
    }

    /*
    c->shandle_head = NULL;
    c->shandle_tail = NULL;
    */

    c->rhandle = NULL;
    c->nextflow = NULL;
    c->inflow = 0;
    c->preposts = 0;
    c->send_wqes_avail = viadev_sq_size;
    /* 
    c->rendezvous_packets_expected = 0;
    */
    c->coalesce_cached_out.context = -1;
    c->coalesce_cached_in.context = -1;

    c->rdma_reads_avail = viadev_default_qp_ous_rd_atom;
    /* reset rdma read pending list */
    c->ext_rdma_read_head = NULL;
    c->ext_rdma_read_tail = NULL;
    /* reset backlog list */
    viadev_init_backlog_queue(c);
    /* reset extended list */
    c->ext_sendq_size = 0;
    c->ext_sendq_head = NULL;
    c->ext_sendq_tail = NULL;
#if defined(ADAPTIVE_RDMA_FAST_PATH)
#ifndef DISABLE_HEADER_CACHING
    /* init cached packet header */
    memset(&(c->cached_outgoing), 0,
            sizeof(viadev_packet_eager_start));
    memset(&(c->cached_incoming), 0,
            sizeof(viadev_packet_eager_start));
#ifdef VIADEV_DEBUG
    c->cached_hit = c->cached_miss = 0;
#endif

    c->cached_incoming.header.type = c->cached_outgoing.header.type
        = FAST_EAGER_CACHED;
#endif
#endif

    c->pending_acks = 0; /* reset all pending stuff */
    c->vi = viadev.qp_hndl[c->global_rank];

    if (QP_DOWN == c->qp_status) {
        c->qp_status = QP_REC;
    }

    if(!viadev_use_srq) {
        /* prepost receives */
        int j;
        for (j = 0; j < viadev_initial_prepost_depth; j++) {
            PREPOST_VBUF_RECV(c);
        }
    }

    c->remote_credit = viadev_initial_credits;
    c->remote_cc = viadev_initial_credits;
    c->local_credit = 0;
    c->preposts = viadev_initial_prepost_depth;

    /* Pasha - need to check, not sure */
    c->pending_r3_data = 0;
    c->received_r3_data = 0;
    /**/

    if(viadev_use_srq) {
        c->initialized = 1;
    } else {
        c->initialized = (viadev_initial_prepost_depth
                ==
                viadev_prepost_depth +
                viadev_prepost_noop_extra);
    }

    CM_DBG("NR: New qp was uploaded to MPI level, c[%d], qp[%x]",
           peer,viadev.qp_hndl[peer]->qp_num);
}


void cm_process_queue(int peer_rank)
{
    cm_pending_request * temp = viadev.pending_req_head[peer_rank];
    cm_pending_request * temp_next;
    while (temp != NULL) {
        if (temp->type == MPID_VIA_SEND) {
            if (temp->len < viadev_rendezvous_threshold &&
                    viadev_eager_ok(temp->len, 
                        &viadev.connections[temp->dest_grank])) {
                MPID_VIA_eager_send(temp->buf, temp->len, temp->src_lrank,
                        temp->tag, temp->context_id, 
                        temp->dest_grank, (MPIR_SHANDLE *)temp->request);

            } else {
                if (NULL == temp->buf || 0 == temp->len) {
                    temp->buf = &nullsbuffer;
                }
                MPID_VIA_rendezvous_start(temp->buf, temp->len, temp->src_lrank,
                        temp->tag, temp->context_id, 
                        temp->dest_grank, (MPIR_SHANDLE *)temp->request);
            }
        } else if (temp->type == MPID_VIA_SEND_RDNV) {
            MPID_VIA_rendezvous_start(temp->buf, temp->len, temp->src_lrank,
                    temp->tag, temp->context_id, 
                    temp->dest_grank, (MPIR_SHANDLE *)temp->request);
        }
        temp_next=temp->next;
        free(temp);
        temp=temp_next;
    }

    viadev.pending_req_head[peer_rank] = 
        viadev.pending_req_tail[peer_rank] = NULL;
}

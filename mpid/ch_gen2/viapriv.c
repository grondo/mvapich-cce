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

#include <stdio.h>
#include "mpid.h"
#include "ibverbs_header.h"
#include "viadev.h"
#include "nfr.h"
#include "viapriv.h"
#include "viutil.h"
#include "vbuf.h"
#include "viapacket.h"
#include <unistd.h>

/* low level VIA information (hidden from ADI2) */
viadev_info_t viadev;

char nullsbuffer, nullrbuffer;

/* The vbuf backlog queue */
viadev_backlog_queue_t *backlog_queue = NULL;

struct ibv_cq *create_cq(struct ibv_comp_channel *ch)
{
    struct ibv_cq *cq_ptr;

    cq_ptr = ibv_create_cq(viadev.context, viadev_cq_size, NULL, ch, 0);

    if (!cq_ptr) {
        error_abort_all(IBV_RETURN_ERR, "Error creating CQ\n");
    }

    return cq_ptr;
}

struct ibv_srq *create_srq()
{
    struct ibv_srq_init_attr srq_init_attr;
    struct ibv_srq *srq_ptr = NULL;

    memset(&srq_init_attr, 0, sizeof(srq_init_attr));

    srq_init_attr.srq_context = viadev.context;
    srq_init_attr.attr.max_wr = viadev_srq_alloc_size;
    srq_init_attr.attr.max_sge = 1;
    /* The limit value should be ignored during SRQ create */
    srq_init_attr.attr.srq_limit = viadev_srq_limit;

#ifdef XRC
    if (viadev_use_xrc) {
        viadev.srqn = (uint32_t *) malloc (viadev.np * sizeof (uint32_t));
        memset(viadev.srqn, 0, viadev.np * sizeof (uint32_t));
        srq_ptr = ibv_create_xrc_srq(viadev.ptag, viadev.xrc_info->xrc_domain, 
                viadev.cq_hndl, &srq_init_attr);
        pmgr_allgather (&(srq_ptr->xrc_srq_num), sizeof (srq_ptr->xrc_srq_num),
                viadev.srqn);
    }    
    else 
#endif 
    { 
        srq_ptr = ibv_create_srq(viadev.ptag, &srq_init_attr);
    }

    if (!srq_ptr) {
        error_abort_all(IBV_RETURN_ERR, "Error creating SRQ\n");
    }

    return srq_ptr;
}

struct ibv_mr* register_memory(void* buf, int len)
{
    return (ibv_reg_mr(viadev.ptag, buf, len,
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
            IBV_ACCESS_REMOTE_READ));
}

int deregister_memory(struct ibv_mr* mr)
{
    return (ibv_dereg_mr(mr));
}

/*
 * There is a limit on number of outstanding RDMA reads
 * this function just extends the rdma_read queue
 */
void viadev_ext_rdma_read_queue(viadev_connection_t * c, vbuf * v)
{
    v->desc.next = NULL;
    if (c->ext_rdma_read_head == NULL) {
        c->ext_rdma_read_head = v;
    } else {
        c->ext_rdma_read_tail->desc.next = v;
    }
    c->ext_rdma_read_tail = v;
}

/* This function just starts the RDMA reads which 
 * were queued before due to lack of outstanding reads
 */
void viadev_ext_rdma_read_start(viadev_connection_t * c)
{
    vbuf *v;
    struct ibv_send_wr *bad_wr;

    while (c->rdma_reads_avail && c->ext_rdma_read_head) {
        /* dequeue one vbuf */
        v = c->ext_rdma_read_head;
        c->ext_rdma_read_head = v->desc.next;
        if (v == c->ext_rdma_read_tail) {
            c->ext_rdma_read_tail = NULL;
        }
        v->desc.next = NULL;
        /* put the vbuf on the HCA's send Q */
        c->rdma_reads_avail--;

        FLUSH_EXT_SQUEUE(c);

        if(!c->send_wqes_avail) {
            viadev_ext_sendq_queue(c, v);
            break;
        }
        c->send_wqes_avail--;

        if(ibv_post_send(c->vi, &(v->desc.u.sr), &bad_wr)) {
            error_abort_all(IBV_RETURN_ERR,"Error posting send\n");
        }
    }
}


/* to handle Send Q overflow, we maintain an extended send queue
 * above the HCA.  This permits use to have a virtually unlimited send Q depth
 * (limited by number of vbufs available for send)
 */
void viadev_ext_sendq_queue(viadev_connection_t * c, vbuf * v)
{
    v->desc.next = NULL;
    if (c->ext_sendq_head == NULL) {
        c->ext_sendq_head = v;
    } else {
        c->ext_sendq_tail->desc.next = v;
    }
    c->ext_sendq_tail = v;

    c->ext_sendq_size++;

    /* add to waiting for ack list */
    NFR_ADD_TO_LIST(&c->waiting_for_ack, v);
}

/* dequeue and send as many as we can from the extended send queue
 * this is called in each function which may post send prior to it attempting
 * its send, hence ordering of sends is maintained
 */
void viadev_ext_sendq_send(viadev_connection_t * c)
{
    vbuf *v;
    struct ibv_send_wr *bad_wr;

    while (c->send_wqes_avail && c->ext_sendq_head) {
        /* dequeue one vbuf */
        v = c->ext_sendq_head;

        if(v->len != 0) {
            viadev_packet_header *h = (viadev_packet_header *) v->buffer;
            assert(h->type == VIADEV_PACKET_EAGER_COALESCE);
            prepare_coalesced_pkt(c, v);
        }

        c->ext_sendq_head = v->desc.next;
        if (v == c->ext_sendq_tail) {
            c->ext_sendq_tail = NULL;
        }
        v->desc.next = NULL;
        /* put the vbuf on the HCA's send Q */
        c->send_wqes_avail--;
        c->ext_sendq_size--;

        if(ibv_post_send(c->vi, &(v->desc.u.sr), &bad_wr)) {
            error_abort_all(IBV_RETURN_ERR,"Error posting send\n");
        }
    }
}

/* init the backlog queue */
void viadev_init_backlog_queue(viadev_connection_t * c)
{
    c->backlog.len = 0;
    c->backlog.vbuf_head = NULL;
    c->backlog.vbuf_tail = NULL;
}

/* 
 * post a descriptor to the send queue
 * all outgoing packets go through this routine. 
 * takes a connection rather than a vi because we need to 
 * update flow control information on the connection. Also
 * it turns out it is always called in the context of a connection, 
 * i.e. it would be post_send(c->vi) otherwise. 
 */

#define BACKLOG_ENQUEUE(q,v) {                                      \
    v->desc.next = NULL;                                            \
        if (q->vbuf_tail == NULL) {                                 \
            q->vbuf_head = v;                                       \
        } else {                                                    \
            q->vbuf_tail->desc.next = v;                            \
        }                                                           \
    q->vbuf_tail = v;                                               \
        q->len++;                                                   \
}


#define BACKLOG_DEQUEUE(q,v)  {                                     \
    v = q->vbuf_head;                                               \
        q->vbuf_head = v->desc.next;                                \
        if (v == q->vbuf_tail) {                                    \
            q->vbuf_tail = NULL;                                    \
        }                                                           \
    q->len--;                                                       \
        v->desc.next = NULL;                                        \
}

/* Just got a credit update, send off as many backlogged
 * messages as possible
 */
void viadev_backlog_send(viadev_connection_t * c)
{
    viadev_backlog_queue_t *q = &c->backlog;
    struct ibv_send_wr *bad_wr;


    while ((q->len > 0) && (c->remote_credit > 0)) {
        vbuf *v = NULL;
        viadev_packet_header *p;
        assert(q->vbuf_head != NULL);
        BACKLOG_DEQUEUE(q, v);

        /* Assumes packet header is at beginning of packet structure */
        p = (viadev_packet_header *) VBUF_BUFFER_START(v);

        PACKET_SET_CREDITS(p, c);
        c->remote_credit--;

        v->grank = c->global_rank;

        if (!c->send_wqes_avail) {
            /* preventing packet dublication on list */
            NFR_REMOVE_FROM_WAITING_LIST((&c->waiting_for_ack), v);
            viadev_ext_sendq_queue(c, v);
            continue;
        }
        c->send_wqes_avail--;

        assert(c->ext_sendq_head == NULL);

        if(ibv_post_send(c->vi, &(v->desc.u.sr), &bad_wr)) {
            error_abort_all(IBV_RETURN_ERR,"Error posting send\n");
        }
    }
}

void viadev_post_send(viadev_connection_t * c, vbuf * v)
{
    struct ibv_send_wr *bad_wr;
    viadev_packet_header *p;
   
    p = (viadev_packet_header *) VBUF_BUFFER_START(v);

#ifdef MEMORY_RELIABLE
    p->crc32 = update_crc(1, (void *) ((char *)VBUF_BUFFER_START(v) + 
                sizeof(viadev_packet_header)),
            v->desc.sg_entry.length - sizeof(viadev_packet_header));
    p->dma_len = v->desc.sg_entry.length - sizeof(viadev_packet_header);
#endif

    v->grank = c->global_rank;

#ifdef _IBM_EHCA_
    /* Inline data transfer not supported on IBM 
     * EHCA */
    v->desc.u.sr.send_flags =
        IBV_SEND_SIGNALED;
#else
    if(v->desc.sg_entry.length < c->max_inline) {
        v->desc.u.sr.send_flags =
            IBV_SEND_SIGNALED | IBV_SEND_INLINE;
    } else {
        v->desc.u.sr.send_flags =
            IBV_SEND_SIGNALED;
    }
#endif

    if(viadev_async_progress) {
      v->desc.u.sr.send_flags = v->desc.u.sr.send_flags|IBV_SEND_SOLICITED;
    }

    if(viadev_use_srq) {
        PACKET_SET_CREDITS(p, c);

        XRC_FILL_SRQN_FIX_CONN (v, c);
        
        FLUSH_EXT_SQUEUE(c);

        if(c->send_wqes_avail <= 0) {
            viadev_ext_sendq_queue(c, v);
            return;
        }

        c->send_wqes_avail--;

        if(ibv_post_send(c->vi, &(v->desc.u.sr), &bad_wr)) {
            error_abort_all(IBV_RETURN_ERR,"Error posting send\n");
        }

        NFR_ADD_TO_LIST(&c->waiting_for_ack, v);

        pthread_spin_lock(&viadev.srq_post_spin_lock);

        if(viadev.posted_bufs <= viadev_credit_preserve) {
            viadev.posted_bufs += 
                viadev_post_srq_buffers(viadev_srq_fill_size - viadev.posted_bufs);
        }

        pthread_spin_unlock(&viadev.srq_post_spin_lock);

    } else {

        /* 
         * will need to grab lock in multithreaded 
         * will need to put sequence number in immediate data for unreliable
         * currently this will tell you if you're out of remote credit
         * but will not block or return error to caller.
         */

        if (c->remote_credit > 0 || p->type == VIADEV_PACKET_NOOP) {
            /* if we got here, the backlog queue better be  empty */
            assert(c->backlog.len == 0 || p->type == VIADEV_PACKET_NOOP);

            PACKET_SET_CREDITS(p, c);
            if (p->type != VIADEV_PACKET_NOOP)
                c->remote_credit--;
            v->grank = c->global_rank;

            FLUSH_EXT_SQUEUE(c);

            if (c->send_wqes_avail <= 0) {
                viadev_ext_sendq_queue(c, v);
                return;
            }
            c->send_wqes_avail--;

            if(ibv_post_send(c->vi, &(v->desc.u.sr), &bad_wr)) {
                error_abort_all(IBV_RETURN_ERR,"Error posting send\n");
            }
            NFR_ADD_TO_LIST(&c->waiting_for_ack, v);

            /* do post receive after post_sr for last part of message
             * so latency hidden during post_sr
             */
            if (viadev_prepost_threshold && v->shandle != NULL
                    && c->initialized) {
                int needed =
                    viadev_prepost_depth + viadev_prepost_noop_extra +
                    MIN(viadev_prepost_rendezvous_extra,
                            c->rendezvous_packets_expected);
                while (c->preposts < (int) viadev_rq_size && c->preposts < needed) {
                    PREPOST_VBUF_RECV(c);
                }
                viadev_send_noop_ifneeded(c);
            }
        } else {
            /* must delay the send until more credits arrive.
             * put on end of queue
             */
            viadev_backlog_queue_t *q = &c->backlog;
            BACKLOG_ENQUEUE(q, v);
            NFR_ADD_TO_LIST(&c->waiting_for_ack, v);

            D_PRINT("post_send QUEUED: [vi %d][%d %d %d][vbuf %p] %s\n",
                    c->global_rank, c->remote_credit, c->local_credit,
                    c->remote_cc, v, viadev_packet_to_string(c->vi, v));
        }

    }
}

#ifdef ADAPTIVE_RDMA_FAST_PATH

void post_fast_rdma_with_completion(viadev_connection_t * c, int len)
{
    struct ibv_send_wr *bad_wr;
    int n = c->phead_RDMA_send;
    VBUF_FLAG_TYPE flag;
    viadev_packet_header *p = (viadev_packet_header *)
        (c->RDMA_send_buf[n].buffer);

    /* Mark RDMA buffer busy */
    c->RDMA_send_buf[n].padding = BUSY_FLAG;

    /* set credits info */
#if (!defined(DISABLE_HEADER_CACHING) && defined(ADAPTIVE_RDMA_FAST_PATH))
    if ((p->type != FAST_EAGER_CACHED) &&
        (p->type != VIADEV_PACKET_EAGER_START))
        PACKET_SET_CREDITS(p, c);

    if (VIADEV_UNLIKELY((p->type == VIADEV_PACKET_EAGER_START) &&
        ((len - sizeof(viadev_packet_eager_start)) >= 
         viadev_max_fast_eager_size)))
        PACKET_SET_CREDITS(p, c);

#else
    PACKET_SET_CREDITS(p, c);
#endif

    if (VIADEV_UNLIKELY(++(c->phead_RDMA_send) >= viadev_num_rdma_buffer))
        c->phead_RDMA_send = 0;

#ifdef _IA64_
    len += 16;
    len -= (len & 7);
#endif

    /* set flags */
    if ((int) *(VBUF_FLAG_TYPE *) (c->RDMA_send_buf[n].buffer + len) == len) {
        flag = (VBUF_FLAG_TYPE) (len + FAST_RDMA_ALT_TAG);
    } else {
        flag = (VBUF_FLAG_TYPE) len;
    }

    /* set head flag */
    *(c->RDMA_send_buf[n].head_flag) = flag;
    /* set tail flag */
    *(VBUF_FLAG_TYPE *) (c->RDMA_send_buf[n].buffer + len) = flag;

    c->num_no_completion = 0;
    /* generate a completion */
    c->RDMA_send_buf[n].desc.u.sr.send_flags = IBV_SEND_SIGNALED;
    c->RDMA_send_buf[n].desc.u.sr.wr_id = (aint_t) & (c->RDMA_send_buf[n]);

    c->RDMA_send_buf[n].desc.sg_entry.length =
        len + VBUF_FAST_RDMA_EXTRA_BYTES;

    XRC_FILL_SRQN (c->RDMA_send_buf[n].desc.u.sr, c->global_rank);
    
    FLUSH_EXT_SQUEUE(c);

    if (VIADEV_UNLIKELY(!c->send_wqes_avail)) {
        viadev_ext_sendq_queue(c, &c->RDMA_send_buf[n]);
        return;
    }
    c->send_wqes_avail--;

#ifdef _IBM_EHCA_
    /* Inline Data transfer not supported on IBM
     * EHCA */    
    c->RDMA_send_buf[n].desc.u.sr.send_flags =
            IBV_SEND_SIGNALED;
#else
    if(c->RDMA_send_buf[n].desc.sg_entry.length < c->max_inline) {
        c->RDMA_send_buf[n].desc.u.sr.send_flags =
            IBV_SEND_SIGNALED | IBV_SEND_INLINE;
    } else {
        c->RDMA_send_buf[n].desc.u.sr.send_flags =
            IBV_SEND_SIGNALED;
    }
#endif

    if(VIADEV_UNLIKELY(ibv_post_send(c->vi,
                &(c->RDMA_send_buf[n].desc.u.sr), &bad_wr))) {
        error_abort_all(IBV_RETURN_ERR,"Error posting send"
                " post size : %d, inline size : %d\n",
                c->RDMA_send_buf[n].desc.sg_entry.length,
                c->max_inline);
    }
    NFR_ADD_TO_LIST_FAST(&c->waiting_for_ack, &c->RDMA_send_buf[n]);
}

int fast_rdma_ok(viadev_connection_t * c, int data_size,
        int is_eager_packet)
{
    /* not OK if buffers are full */
    if (VIADEV_UNLIKELY(viadev_num_rdma_buffer == 0)) {
        return 0;
    }

    if(VIADEV_UNLIKELY(!viadev.initialized)) {
        return 0;
    }

    if (VIADEV_UNLIKELY(c->phead_RDMA_send == c->ptail_RDMA_send)) {
        return 0;
    }

    /* not OK if data size is too large */
    if (VIADEV_UNLIKELY(data_size > (int) VBUF_BUFFER_SIZE)) {
        return 0;
    }

    /* not OK if there are sends waiting in the backlog ??? */
    if (VIADEV_UNLIKELY(c->backlog.len > 0)) {
        return 0;
    }

    if (VIADEV_UNLIKELY(c->RDMA_send_buf == NULL)) {
        return 0;
    }
    
    /* See if head buffer is marked free */
    if(VIADEV_UNLIKELY(c->RDMA_send_buf[c->phead_RDMA_send].padding == BUSY_FLAG)) {
        return 0;
    }

    /* otherwise OK */
    return 1;
#if 0
    int nvbufs, avail_rdma_bufs;

    /* not OK if no rdma buffers */
    if (viadev_num_rdma_buffer == 0) {
        return 0;
    }

    /* not OK if buffers are full */
    if (c->phead_RDMA_send == c->ptail_RDMA_send) {
        return 0;
    }

    /* not OK if there are sends waiting in the backlog ??? */
    if (c->backlog.len > 0) {
        return 0;
    }

    /* OK if data size < vbuf size */
    if (data_size < VBUF_BUFFER_SIZE) {
        return 1;
    }

    if(is_eager_packet) {
        nvbufs = viadev_calculate_vbufs_expected(
                data_size - sizeof(viadev_packet_eager_start),
                VIADEV_PROTOCOL_EAGER);
        /* are these many vbufs available? */

        if(c->phead_RDMA_send < c->ptail_RDMA_send) {
            avail_rdma_bufs = c->ptail_RDMA_send 
                - c->phead_RDMA_send + 1;
        } else {
            avail_rdma_bufs = viadev_num_rdma_buffer - 
                c->phead_RDMA_send + c->ptail_RDMA_send;
        }

        return (avail_rdma_bufs>nvbufs ? 1 : 0);
    } else {
        /* Control should never reach here,
         * all packets which are not eager,
         * Only EAGER packets should be
         * packetized. All other control msgs
         * should go only over 1 packet.
         */
        return 0;
    }

    /* otherwise OK */
    return 1;
#endif
}

void release_recv_rdma(viadev_connection_t * c, vbuf * v)
{
    vbuf *next_free;
    int next;
    int i;

    next = c->p_RDMA_recv_tail + 1;
    if (next >= viadev_num_rdma_buffer)
        next = 0;
    next_free = &(c->RDMA_recv_buf[next]);

    v->padding = FREE_FLAG;
    if (v != next_free) {
        return;
    }

    /* search all free buffers */
    for (i = next; i != c->p_RDMA_recv;) {

        if (c->RDMA_recv_buf[i].padding == FREE_FLAG) {
            c->rdma_credit++;
            if (++(c->p_RDMA_recv_tail) >= viadev_num_rdma_buffer)
                c->p_RDMA_recv_tail = 0;
            c->RDMA_recv_buf[i].padding = BUSY_FLAG;
        } else break;
        if (++i >= viadev_num_rdma_buffer)
            i = 0;
    }
}


#if (!defined(DISABLE_HEADER_CACHING) && \
        defined(ADAPTIVE_RDMA_FAST_PATH))

int search_header_cache(viadev_connection_t * c,
                        viadev_packet_eager_start * h)
{
    viadev_packet_eager_start *cached = &(c->cached_outgoing);
    if (IS_HEADER_CACHED(cached, h)) {
#ifdef VIADEV_DEBUG	    
        c->cached_hit++;
#endif
        return 1;
    }

#ifdef VIADEV_DEBUG	    
    c->cached_miss++;
#endif
    memcpy(cached, h, sizeof(viadev_packet_eager_start));
    return 0;
}

#endif


#endif

void viadev_post_rdmawrite(viadev_connection_t * c, vbuf * v)
{
    struct ibv_send_wr *bad_wr;

    D_PRINT("post_rdmawrite: [vi %d] %s",
            c->global_rank, viadev_packet_to_string(c->vi, v));

    /* set id to be the global_rank */
    v->grank = c->global_rank;

    XRC_FILL_SRQN_FIX_CONN (v, c); 
    
    FLUSH_EXT_SQUEUE(c);

    if (!c->send_wqes_avail) {
        viadev_ext_sendq_queue(c, v);
        return;
    }
    c->send_wqes_avail--;

    /* Don't try to inline this data, since it is large */
    v->desc.u.sr.send_flags = IBV_SEND_SIGNALED;

    if(ibv_post_send(c->vi, &(v->desc.u.sr), &bad_wr)) {
        error_abort_all(IBV_RETURN_ERR,"Error posting send\n");
    }
}

void viadev_post_rdmaread(viadev_connection_t * c, vbuf * v)
{
    struct ibv_send_wr *bad_wr;


    /* set id to be the global_rank */
    v->grank = c->global_rank;
    
    XRC_FILL_SRQN_FIX_CONN (v, c);

    FLUSH_EXT_SQUEUE(c);

    if (!c->rdma_reads_avail) {
        viadev_ext_rdma_read_queue(c, v);
        return;
    }
    c->rdma_reads_avail--;

    if (!c->send_wqes_avail) {
        viadev_ext_sendq_queue(c, v);
        return;
    }

    c->send_wqes_avail--;

    /* Don't try to inline this data, since it is large */
    v->desc.u.sr.send_flags = IBV_SEND_SIGNALED;

    if(ibv_post_send(c->vi, &(v->desc.u.sr), &bad_wr)) {
        error_abort_all(IBV_RETURN_ERR,"Error posting send\n");
    }

    if(viadev_use_apm && viadev_use_apm_test) {
        perform_manual_apm(c->vi);
    }

}


/* 
 * post a descriptor to the receive queue
 */

void viadev_post_recv(viadev_connection_t * c, vbuf * v)
{
    struct ibv_recv_wr *bad_wr;

    D_PRINT("viadev_post_recv: v = \n" AINT_FORMAT, (aint_t) v);
    /* set id to be the global_rank */
    v->grank = c->global_rank;

    
    if(ibv_post_recv(c->vi, &(v->desc.u.rr), &bad_wr)) {
        error_abort_all(IBV_RETURN_ERR,
                "Error posting recv\n");
    }
}

/*
 * post a list of descriptors to the receive queue
 */

void viadev_post_recv_list(viadev_connection_t *c, int num)
{
    struct ibv_recv_wr *bad_wr;

    if(ibv_post_recv(c->vi, viadev.array_recv_desc, &bad_wr)) {
        error_abort_all(IBV_RETURN_ERR,
                "Error posting list descriptors\n");
    }
}

/*
 * Collect a vbuf on a list so that many can be
 * posted together
 *
 * index: This is the place in the array where this
 * vbuf wil be placed
 */

void collect_vbuf_for_recv(int index, viadev_connection_t *c)
{
    vbuf *v;

    v = get_vbuf();

    vbuf_init_recv(v, VBUF_BUFFER_SIZE);

    v->grank = c->global_rank;
    c->local_credit++;
    c->preposts++;
    

    memcpy(&viadev.array_recv_desc[index],
            &v->desc.u.rr, sizeof(struct ibv_recv_wr));

    /* Point the previous descriptor to this one
     * so as to create a chain */
    if(index > 0) {
        viadev.array_recv_desc[index-1].next =
            &viadev.array_recv_desc[index];
    }

    /* Null terminate the last descriptor link */
    viadev.array_recv_desc[index].next = NULL;
}

char *viadev_packet_to_string(struct ibv_qp *vi, vbuf * v)
{
    return '\0';
}

int vi_to_grank(struct ibv_qp *vi)
{
    return 0;
}

int viadev_post_srq_buffers(int num_bufs)
{
    int i = 0;
    vbuf *v;
    struct ibv_recv_wr *bad_wr;

    if(num_bufs > viadev_srq_fill_size) {
        error_abort_all(GEN_ASSERT_ERR,"Try to post %d to SRQ, max %d\n",
                num_bufs, viadev_srq_fill_size);
    }


    for(i = 0; i < num_bufs; i++) {

        v = get_vbuf();

        if(NULL == v) {
            break;
        }

        vbuf_init_recv(v, VBUF_BUFFER_SIZE);

        if(ibv_post_srq_recv(viadev.srq_hndl,
                    &v->desc.u.rr, &bad_wr)) {
            release_vbuf(v);
            break;
        }
    }

    return i;
}

#ifdef MEMORY_RELIABLE

/* init all CRC queues to NULL */
void init_crc_queues(void)
{
    int i;
    for (i = 0; i < viadev.np; i++) {
        viadev_connection_t *c = &viadev.connections[i];
        c->crc_queue = NULL;
    }
}

/* search the CRC queue for vbufs with given sequence id */
vbuf* search_crc_queue(viadev_connection_t *c, packet_sequence_t id)
{
    vbuf *v, *v_next;
    viadev_packet_header *header;
    v = c->crc_queue;

    header = (viadev_packet_header *) VBUF_BUFFER_START(v);

    if (header->id == id) {
        return v;
    }

    v_next = (vbuf *)(v->desc.next);
    while (v_next) {
        header = (viadev_packet_header *) VBUF_BUFFER_START(v_next);
        if (header->id == id) {
            return v_next;
        }
        v = v_next;
        v_next = (vbuf *)(v_next->desc.next);
    }
    return NULL;
}

/* Enqueue the given vbuf into the crc_queue
   Called whenever a send completion from device occurs
   vbufs are enqueued in an increasing order of ids 
   Entries are of vbufs which have outstanding acks*/

int enqueue_crc_queue(vbuf *vbuf_addr, viadev_connection_t *c)
{

    vbuf* v;
    viadev_packet_header *header;
    v = c->crc_queue;

    header = (viadev_packet_header *) VBUF_BUFFER_START(vbuf_addr);
    if (header->id <= c->max_seq_id_acked){
        release_vbuf(vbuf_addr);
        return 1;
    }

    if (v == NULL){
        c->crc_queue = vbuf_addr;
        vbuf_addr->desc.next = NULL;
        return 1;
    }
    else{
        while (v->desc.next != NULL){v = v->desc.next;}; 
        v->desc.next = vbuf_addr;
        vbuf_addr->desc.next = NULL;
        return 1;
    }
}


/* Dequeue the vbufs with sequence id <= acked_sequence_id 
   Called whenever an ack is received */

int dequeue_crc_queue(packet_sequence_t ack, viadev_connection_t *c)
{
    vbuf* v, *v_next;
    viadev_packet_header *header;

    v = c->crc_queue;
    c->max_seq_id_acked = ack;
    if (v == NULL){
        return 0;
    }

    do{
        header = (viadev_packet_header *) VBUF_BUFFER_START(v);
        if (header->id <= ack){
            v_next = v->desc.next;
            release_vbuf(v);
            v = v_next;
        }
        else {
            c->crc_queue = v; 
            break;
        }
    } while (v != NULL);

    if (v == NULL) 
        c->crc_queue = NULL;
    return 1;
} 

/* check ofo queues of all connections to see if
 * there is any packet (vbuf) that matches the current
 * expected sequence number
 * */

int check_ofo_queues(void **vbuf_addr)
{
    int i;
    viadev_packet_header *h;
    vbuf *v;
    for (i = 0; i < viadev.np; i++) {
        viadev_connection_t *c = &viadev.connections[i];
        if (i == viadev.me) {
            continue;
        }
        /* check the ofo queue */
        v = c->ofo_head;
        while (v) {
            h = (viadev_packet_header *)VBUF_BUFFER_START(v);
            /* in-order packet? */
            if (h->id == c->next_packet_expected) {
                remove_ofo_queue(v, c);
                *vbuf_addr = v;
                return 0;
            }
            v = (vbuf *)(v->desc.next);
        }
    }
    return 1;
}


/* add a packet (vbuf) to an ofo queue */
void enqueue_ofo_queue(vbuf *vbuf_addr,
        viadev_connection_t *c)
{
    vbuf_addr->desc.next = c->ofo_head;
    c->ofo_head = vbuf_addr;
}

void remove_ofo_queue(vbuf *vbuf_addr, viadev_connection_t *c)
{
    vbuf *v, *v_next;

    v = c->ofo_head;
    if (v == NULL) {
        error_abort_all(GEN_ASSERT_ERR, 
                "remove_ofo_queue error, queue empty! vbuf = %p\n",
                vbuf_addr);
    }

    if (v == vbuf_addr) {
        c->ofo_head = (vbuf *)(v->desc.next);
        return;
    }

    v_next = (vbuf *)(v->desc.next);
    while (v_next) {
        if (v_next == vbuf_addr) {
            v->desc.next = v_next->desc.next;
            return;
        }
        v = v_next;
        v_next = (vbuf *)(v_next->desc.next);
    }

    error_abort_all(GEN_ASSERT_ERR, 
            "remove_ofo_queue error, not found! vbuf = %p\n",
            vbuf_addr);
}


/* init all ofo queues to NULL */
void init_ofo_queues(void)
{
    int i;
    for (i = 0; i < viadev.np; i++) {
        viadev_connection_t *c = &viadev.connections[i];
        c->ofo_head = NULL;
    }
}


#endif

#ifdef MCST_SUPPORT
void viadev_post_ud_recv(vbuf *);

void viadev_post_ud_recv(vbuf * v)
{

    int result;
    struct ibv_recv_wr *bad_wr;

    v->grank = -1;

    D_PRINT("viadev_post_ud_recv: v = " AINT_FORMAT, (aint_t) v);
    if (ibv_post_recv (viadev.ud_qp_hndl, &(v->desc.u.rr), &bad_wr)){
        error_abort_all(IBV_RETURN_ERR,
                "Error posting recv\n");
    }
}

#endif

/*
 * This function makes sure that all
 * pending sends and receives are
 * completed before the device is
 * finalized.
 *
 */

void flush_all_messages()
{
    int i;

    /* Barrier makes sure all receives are completed,
     * but doesn't make sure all pending sends have.
     * So, make sure all sends complete */

    for (i = 0; i < viadev.np; i++) {
        if (i == viadev.me) {
            continue;
        }
        if (viadev_use_on_demand) {
            if (MPICM_IB_RC_PT2PT != cm_conn_state[i]) {
                continue;
            }
        }

        do {
            while (viadev.connections[i].send_wqes_avail < viadev_sq_size) {
                MPID_DeviceCheck(MPID_NOTBLOCKING);
            }

            if(viadev.connections[i].ext_sendq_head != NULL) {
                viadev_ext_sendq_send(&viadev.connections[i]);
            }
        } while(viadev.connections[i].ext_sendq_head != NULL);
    }


    /* If MEMORY_RELIABLE is defined, some acknowledgement
     * messages for the final barrier message might still
     * be pending. Complete those now */

#ifdef MEMORY_RELIABLE

    for (i = 0; i < viadev.np; i++) {
        if (i == viadev.me) {
            continue;
        }
        if (viadev_use_on_demand) {
            if (MPICM_IB_RC_PT2PT != cm_conn_state[i]) {
                continue;
            }
        }
        while ((viadev.connections[i].max_seq_id_acked < 
                    (viadev.connections[i].max_datapkt_seq)) || 
                (viadev.connections[i].send_wqes_avail < viadev_sq_size)){
            MPID_DeviceCheck(MPID_NOTBLOCKING);
        }
    }
#endif
}

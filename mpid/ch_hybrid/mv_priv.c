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
#include <unistd.h>
#include "mpid.h"
#include "ibverbs_header.h"
#include "mv_dev.h"
#include "mv_priv.h"
#include "mv_inline.h"
#include "mv_buf.h"
#include "mv_packet.h"

mvdev_info_t mvdev;
mv_func mvdev_func;
char nullsbuffer, nullrbuffer;
uint8_t mvdev_header_size;

struct ibv_mr* register_memory(int hca_num, void* buf, int len)
{
    return (ibv_reg_mr(mvdev.hca[hca_num].pd, buf, len,
                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
}

int deregister_memory(struct ibv_mr* mr)
{
    return (ibv_dereg_mr(mr));
}

void flush_all_messages() {
    /* need to put the necessary code here */
}

#define PREPARE_RECV(v, rp) {                                        \
    v->desc.rr.next = NULL;                                         \
    v->desc.rr.wr_id = (aint_t) &(v->desc);                         \
    v->desc.rr.num_sge = 1;                                         \
    v->desc.rr.sg_list = &(v->desc.sg_entry);                       \
    v->desc.sg_entry.addr = (uintptr_t) (v->base_ptr);              \
    v->desc.sg_entry.length = v->buf->region->size_ptr->alloc_size; \
    v->desc.sg_entry.lkey = v->buf->region->mem_handle[0]->lkey;    \
    v->rpool = rp;                                                  \
}

static inline void prepare_rc_recv(mv_rbuf *v, mv_rpool *rp) {
    v->mheader_ptr = v->base_ptr;
    v->transport = MVDEV_TRANSPORT_RC;
    PREPARE_RECV(v, rp);
}

static inline void prepare_ud_recv(mv_rbuf *v, mv_rpool *rp) {
    v->mheader_ptr = v->base_ptr + 40;
    v->transport = MVDEV_TRANSPORT_UD;
    PREPARE_RECV(v, rp);
}

int mvdev_post_rq_buffers(mv_rpool * rp, mv_qp * qp, int num_bufs)
{
    int i = 0, total = 1;
    mv_rbuf *v, *first_v, *last_v;
    struct ibv_recv_wr *bad_wr;

    D_PRINT("Attempting to post %d recv bufs\n", num_bufs);

    first_v = last_v = get_mv_rbuf(rp->buffer_size);

    switch(qp->type) {
        case MVDEV_CH_RC_RQ:
            prepare_rc_recv(first_v, rp);
            break;
        case MVDEV_CH_UD_RQ:
            prepare_ud_recv(first_v, rp);
            break;
        default:
            error_abort_all(IBV_RETURN_ERR,"Invalid QP Type: %d\n", qp->type);
    }

    for(i = 1; i < num_bufs; i++) {
        ++total;
        v = get_mv_rbuf(rp->buffer_size);

        switch(qp->type) {
            case MVDEV_CH_RC_RQ:
                prepare_rc_recv(v, rp);
                break;
            case MVDEV_CH_UD_RQ:
                prepare_ud_recv(v, rp);
                break;
        }

        last_v->desc.rr.next = &v->desc.rr;
        last_v = v;
    }

    if(ibv_post_recv(qp->qp, &first_v->desc.rr, &bad_wr)) {
        return 0;
    }       

    D_PRINT("Posted %d recvs\n", i);

    return total;
}

int mvdev_post_srq_buffers(mv_rpool *rp, mv_srq * srq, int num_bufs)
{
    int i = 0, total = 1;
    mv_rbuf *v, *first_v, *last_v;
    struct ibv_recv_wr *bad_wr;

    first_v = last_v = get_mv_rbuf(srq->buffer_size);
    prepare_rc_recv(first_v, rp);

    for(i = 1; i < num_bufs; i++) {
        ++total;
        v = get_mv_rbuf(srq->buffer_size);
        prepare_rc_recv(v, rp);

        last_v->desc.rr.next = &v->desc.rr;
        last_v = v;
    }

    if(MVDEV_UNLIKELY(ibv_post_srq_recv(srq->srq, &first_v->desc.rr, &bad_wr))) {
        fprintf(stderr, "Cannot post to SRQ!\n");
        return 0;
        /* we should know if this happens */
    }       

    D_PRINT("Posted %d recvs to SRQ\n", i);

    return total;
}


void mvdev_windowq_queue(mvdev_connection_t * c, mv_sbuf * v, int total_len)
{
    D_PRINT("Window q\n");
    v->seqnum = total_len;
    v->extwin_ptr.next = NULL; 
    if (c->ext_window_head == NULL) { 
        c->ext_window_head = v;
    } else {    
        c->ext_window_tail->extwin_ptr.next = v;
    }
    c->ext_window_tail = v;
}

void mvdev_ext_sendq_queue(mv_qp *qp, mv_sdescriptor * d)
{
    D_PRINT("ext_sendq add");
    ++(qp->ext_sendq_size);
    d->next_extsendq = NULL; 
    if(qp->ext_sendq_head == NULL) { 
        qp->ext_sendq_head = d;
    } else {    
        qp->ext_sendq_tail->next_extsendq = d;
    }

    qp->ext_sendq_tail = d;
}

void mvdev_ext_backlogq_queue(mv_qp *qp, mv_sdescriptor * d)
{
    D_PRINT("backlogq add");
    d->next_extsendq = NULL; 
    if(qp->ext_backlogq_head == NULL) { 
        qp->ext_backlogq_head = d;
    } else {    
        qp->ext_backlogq_tail->next_extsendq = d;
    }
    qp->ext_backlogq_tail = d;

    /* TODO: if this has credit... send ack  
    mvdev_explicit_ack(((mv_sbuf *)d->parent)->rank);
    */

    mvdev.connections[((mv_sbuf *)d->parent)->rank].queued++;
    D_PRINT("now %d queued\n", mvdev.connections[((mv_sbuf *)d->parent)->rank].queued);

    if(mvdev.connections[((mv_sbuf *)d->parent)->rank].queued > 25) {
       mvdev.connections[((mv_sbuf *)d->parent)->rank].msg_info.control_ignore++;
        mvdev_explicit_ack(((mv_sbuf *)d->parent)->rank);
    }

}

void mvdev_ext_backlogq_send(mv_qp * qp)
{
    mv_sdescriptor *d;
    struct ibv_send_wr *sr;
    struct ibv_send_wr *bad_wr;
    int i;

    while (qp->send_credits_remaining > 0 && qp->ext_backlogq_head) {
        d = qp->ext_backlogq_head;

        /* find how many desc are chained */
        i = 1;
        sr = &(d->sr);
        while(sr->next) {
            sr = sr->next;
            i++;
        }
        assert(i == 1);

        if(qp->send_credits_remaining >= i) {
            qp->ext_backlogq_head = d->next_extsendq;
            if (d == qp->ext_backlogq_tail) {
                qp->ext_backlogq_tail = NULL;
            }
            d->next_extsendq = NULL;

            mvdev.connections[((mv_sbuf *)d->parent)->rank].queued--;

            /* reset the credit counter now  -- so we don't lose credits in
             * the backlogq */
            if(MVDEV_RPUT_FLAG == ((mv_sbuf *)d->parent)->flag) {
                D_PRINT("unqueing RPUT\n");
            } else {
                PACKET_SET_CREDITS(((mv_sbuf *)d->parent), (&(mvdev.connections[((mv_sbuf *) d->parent)->rank])));
            }

            D_PRINT("at %d, dropping to %d, queued: %d\n", qp->send_credits_remaining,
                    qp->send_credits_remaining - i, mvdev.connections[((mv_sbuf *)d->parent)->rank].queued);
            qp->send_credits_remaining -= i;

            if((qp->send_wqes_avail - i) < 0 || (NULL != qp->ext_sendq_head)) {
                mvdev_ext_sendq_queue(qp, d);
            } else {
                if(ibv_post_send(qp->qp, &(d->sr), &bad_wr)) {
                    error_abort_all(IBV_RETURN_ERR,"Error posting to RC QP (%d)\n", qp->send_wqes_avail);
                }
                qp->send_wqes_avail -= i;
            }
        } else {
            break;
        }
    }
}

void mvdev_ext_sendq_send(mv_qp * qp)
{
    mv_sdescriptor *d;
    struct ibv_send_wr *sr;
    struct ibv_send_wr *bad_wr;
    int i;

    while (qp->send_wqes_avail > 0 && qp->ext_sendq_head) {
        d = qp->ext_sendq_head;

        /* find how many desc are chained */
        i = 1;
        sr = &(d->sr);
        while(sr->next) {
            sr = sr->next;
            i++;
        }

        if(qp->send_wqes_avail >= i) {
            qp->ext_sendq_size--;

            qp->ext_sendq_head = d->next_extsendq;
            if (d == qp->ext_sendq_tail) {
                qp->ext_sendq_tail = NULL;
            }
            d->next_extsendq = NULL;
            qp->send_wqes_avail -= i;

            if(ibv_post_send(qp->qp, &(d->sr), &bad_wr)) {
                error_abort_all(IBV_RETURN_ERR,"Error posting to UD QP\n");
            }
        } else {
            break;
        }
    }
}


void mvdev_windowq_send(mvdev_connection_t * c)
{
    mv_sbuf * next;
    while(NULL != c->ext_window_head &&
            c->send_window_segments <= mvparams.send_window_segments_max) {
        next = c->ext_window_head->extwin_ptr.next;
        mvdev_post_channel_send(c, c->ext_window_head,
                c->ext_window_head->seqnum);
        c->ext_window_head = next;
    }

    if(c->ext_window_head == NULL) {
        c->ext_window_tail = NULL;
    }
}



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

static inline void prepare_rcfp_descriptor_headers(mv_sdescriptor *desc, unsigned len, mv_qp *qp) {
    if(len <= qp->max_inline) {
        desc->sr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
    } else {
        desc->sr.send_flags = IBV_SEND_SIGNALED;
    }

    /* we will need to set the key if using more than one HCA */
    desc->qp = qp;
    desc->sg_entry.length = len;
}

#define PREPARE_RCFP(_desc, _len, _qp) { \
    if((_len) <= (_qp)->max_inline) { \
        (_desc)->sr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE; \
    } else { \
        (_desc)->sr.send_flags = IBV_SEND_SIGNALED; \
    } \
    (_desc)->qp = (_qp); \
    (_desc)->sg_entry.length = (_len); \
}

/* we need a function that will not replicate the type, rdma_credt, etc */

void MV_Send_RCFP_Cached(mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *qp) {
    MVBUF_HEAD_FLAG_TYPE * rcfp_header = (MVBUF_HEAD_FLAG_TYPE *) v->base_ptr;
    MVBUF_TAIL_FLAG_TYPE * rcfp_tail = (MVBUF_TAIL_FLAG_TYPE *) 
        (v->base_ptr + total_len + sizeof(MVBUF_HEAD_FLAG_TYPE));
    uint64_t thead, tlen, tseqnum, ttype, tlast_recv, tail_val;

    total_len += MV_RCFP_OVERHEAD; 

    D_PRINT("sending cached\n");

    v->seqnum = v->seqnum_last = c->seqnum_next_tosend;
    INCREMENT_SEQ_NUM(&(c->seqnum_next_tosend));

    tlen = total_len; tseqnum = v->seqnum;
    ttype = MVDEV_PACKET_FP_CACHED_EAGER;
    tlast_recv = c->seqnum_next_toack;

    READ_RCFP_TAIL_VAL(*rcfp_tail, (&tail_val));
    thead = (tail_val == MVBUF_FLAG_VAL) ? MVBUF_FLAG_VAL_ALT : MVBUF_FLAG_VAL;

    *rcfp_header = CREATE_RCFP_HEADER(thead, tlen, tseqnum);
    *rcfp_tail = CREATE_RCFP_FOOTER(thead, ttype, tlast_recv);

    v->segments = v->left_to_send = 1;
    v->header = 2;

    mvdev.connection_ack_required[c->global_rank] = 0;

    PREPARE_RCFP(&(v->desc), total_len, qp);
    SEND_RC_SR(qp, v);

    v->total_len = total_len;

    mvdev_track_send(c, v);
    RESET_CREDITS(v, c);

    COLLECT_RCFP_INTERNAL_MSG_INFO(total_len, MVDEV_PACKET_FP_CACHED_EAGER, c);
}

void MV_Send_RCFP_Normal(
        mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *send_qp) {

    MVBUF_HEAD_FLAG_TYPE * rcfp_header = (MVBUF_HEAD_FLAG_TYPE *) v->base_ptr;
    MVBUF_TAIL_FLAG_TYPE * rcfp_tail =
        (MVBUF_TAIL_FLAG_TYPE *) (v->base_ptr + total_len + sizeof(MVBUF_HEAD_FLAG_TYPE));
    uint64_t thead, tlen, tseqnum, tail_val;

    mv_qp *qp = send_qp;
    total_len += MV_RCFP_OVERHEAD; 

    D_PRINT("Sending over RCFP seqnum: %d\n", c->seqnum_next_tosend);
    v->seqnum = v->seqnum_last = c->seqnum_next_tosend;
    INCREMENT_SEQ_NUM(&(c->seqnum_next_tosend));

    /* don't try to pack everything in yet */
    tlen = total_len; tseqnum = v->seqnum;

    READ_RCFP_TAIL_VAL(*rcfp_tail, (&tail_val));
    thead = (tail_val == MVBUF_FLAG_VAL) ? MVBUF_FLAG_VAL_ALT : MVBUF_FLAG_VAL;

    *rcfp_header = CREATE_RCFP_HEADER(thead, tlen, tseqnum);
    *rcfp_tail = CREATE_RCFP_FOOTER(thead, (uint64_t)63, (uint64_t)0);

    v->segments = 1;
    v->left_to_send = 1;

    mvdev.connection_ack_required[c->global_rank] = 0;

    prepare_rcfp_descriptor_headers(&(v->desc), total_len, qp);
    SEND_RC_SR(qp, v);

    v->rel_type = MV_RELIABLE_LOCK_SBUF;
    v->total_len = total_len;

    mvdev_track_send(c, v);

    RESET_CREDITS(v, c);

#ifdef MV_PROFILE
    {
        mvdev_packet_header * p = (mvdev_packet_header *) v->header_ptr;
        COLLECT_RCFP_INTERNAL_MSG_INFO(total_len, p->type, c);
    }
#endif
}



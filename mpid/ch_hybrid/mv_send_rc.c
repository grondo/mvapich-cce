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


static inline void prepare_rc_descriptor_headers(mv_sdescriptor *desc, 
        unsigned char * addr, mv_sbuf *sbuf, unsigned len, mv_qp * qp)
{
    desc->sr.next = NULL;

    SET_INLINE(desc, len, qp);

    desc->sr.opcode = IBV_WR_SEND;
    desc->sr.wr_id = (aint_t) desc;
    desc->sr.num_sge = 1;
    desc->sr.sg_list = &(desc->sg_entry);
    desc->qp = qp;
    desc->sg_entry.addr = (uintptr_t) addr;
    desc->sg_entry.length = len;
    desc->sg_entry.lkey = sbuf->buf->region->mem_handle[0]->lkey;
}

static inline void prepare_rc_descriptor(mv_sdescriptor *desc, 
        unsigned char * addr, mv_sbuf *sbuf, unsigned len, uint32_t seqnum, 
        uint32_t has_header, mv_qp * qp)
{
    desc->sr.next = NULL;

    SET_INLINE(desc, len, qp);

    desc->sr.opcode = IBV_WR_SEND_WITH_IMM;
    desc->sr.wr_id = (aint_t) desc;
    desc->sr.num_sge = 1;
    desc->sr.sg_list = &(desc->sg_entry);
    desc->sr.imm_data = CREATE_THS_TUPLE(has_header, mvdev.me, seqnum);
    desc->qp = qp;
    desc->sg_entry.addr = (uintptr_t) addr;
    desc->sg_entry.length = len;
    desc->sg_entry.lkey = sbuf->buf->region->mem_handle[0]->lkey;
}

void MV_Send_RC_Normal(
        mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *send_qp) 
{

    mvdev_packet_mheader * mp = (mvdev_packet_mheader *) v->base_ptr;
    mv_qp *qp = send_qp;

    total_len += MV_HEADER_OFFSET_SIZE; 

    MV_ASSERT(total_len <= qp->max_send_size);

    v->seqnum = v->seqnum_last = c->seqnum_next_tosend;
    INCREMENT_SEQ_NUM(&(c->seqnum_next_tosend));
    SET_MHEADER(mp, v->seqnum, 1);

    v->rank = c->global_rank;
    v->segments = v->left_to_send = 1;
    v->total_len = total_len;

    MARK_ACK_COMPLETED(c->global_rank);

    prepare_rc_descriptor_headers(&(v->desc), v->base_ptr, v, total_len, qp);

    if(MVDEV_UNLIKELY(MVDEV_CH_RC_RQ == qp->type)) {
        if(((qp->send_credits_remaining <= 0) || (qp->ext_backlogq_head))) { 
            mvdev_ext_backlogq_queue(qp, &(v->desc));  
        } else {
            --(qp->send_credits_remaining);
            SEND_RC_SR(qp, v);
            RESET_CREDITS(v, c);
        }

        v->rel_type = MV_RELIABLE_FREE_SBUF;
        if(mvparams.rc_rel)
            v->rel_type = MV_RELIABLE_FULL_X;
    } else {
        v->rel_type = MV_RELIABLE_NONE;
        SEND_RC_SR(qp, v);
        RESET_CREDITS(v, c);
    }

    mvdev_track_send(c, v);

#ifdef MV_PROFILE
    {
        mvdev_packet_header * p = (mvdev_packet_header *) v->header_ptr; 
        COLLECT_RC_INTERNAL_MSG_INFO(total_len, p->type, c);
    }
#endif
}



void MV_Send_RC_Imm(
        mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *send_qp) 
{
    mvdev_packet_header * p = (mvdev_packet_header *) v->header_ptr;
    int len = 0, seqnum, bytes_sent = 0;
    unsigned char * current_pos = v->base_ptr;
    mv_qp *qp = send_qp;

    SEND_WIN_CHECK(c, v, total_len);

    v->seqnum = c->seqnum_next_tosend;
    v->rank = c->global_rank;
    v->segments = 0;
    v->left_to_send = 0;

    MARK_ACK_COMPLETED(c->global_rank);
    
    /* need to allocate as many sdescriptors as are required */
    v->desc_chunk = 
        get_sdescriptor_chunk(ceil(1.0 * total_len / qp->max_send_size));
    
    do {
        seqnum = v->seqnum_last = c->seqnum_next_tosend;
        INCREMENT_SEQ_NUM(&(c->seqnum_next_tosend));

        len = MIN(qp->max_send_size, total_len - bytes_sent);

        prepare_rc_descriptor(&(v->desc_chunk->desc[v->segments]), current_pos,
                v, len, seqnum, (v->segments == 0), qp);

        v->desc_chunk->desc[v->segments].parent = v;

        if(v->segments > 0) {
            v->desc_chunk->desc[v->segments - 1].sr.next = 
                &(v->desc_chunk->desc[v->segments].sr);
        }

        current_pos += len;
        bytes_sent += len;

        ++(v->segments);
        ++(v->left_to_send);
    } while(bytes_sent < total_len) ;


    if(MVDEV_UNLIKELY(MVDEV_CH_RC_RQ == qp->type)) {
        if(MVDEV_UNLIKELY(qp->send_credits_remaining < v->segments) || (qp->ext_backlogq_head)) {
            mvdev_ext_backlogq_queue(qp, &(v->desc_chunk->desc[0]));
        } else {
            qp->send_credits_remaining -= v->segments;
            D_PRINT("sending chunk: %d, so now: %d\n", v->segments, qp->send_credits_remaining);
            SEND_RC_SR_CHUNK(qp, v, 0, v->segments);
            RESET_CREDITS(v, c);
        }
        v->rel_type = MV_RELIABLE_FREE_SBUF;
        if(mvparams.rc_rel)
            v->rel_type = MV_RELIABLE_FULL_X;
    } else {
        SEND_RC_SR_CHUNK(qp, v, 0, v->segments);
        v->rel_type = MV_RELIABLE_NONE;
        RESET_CREDITS(v, c);
    }

    mvdev_track_send(c, v);

    if(MVDEV_UNLIKELY(MVDEV_PACKET_R3_DATA == p->type)) {
        c->r3_segments += v->segments;
    }

    COLLECT_RC_INTERNAL_MSG_INFO(total_len, p->type, c);
}



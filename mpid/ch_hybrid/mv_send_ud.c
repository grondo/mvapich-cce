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


#define GET_UD_QP(_toset, _passed) {                                                        \
    if(MVDEV_LIKELY(NULL == _passed)) {                                                     \
        mvdev.last_qp = (mvdev.last_qp + 1) % mvdev.num_ud_qps;                             \
        _toset = &(mvdev.ud_qp[mvdev.last_qp]);                                             \
    }                                                                                       \
}

#define SET_INLINE(_desc, _len, _qp) { \
    if((_len) <= (_qp)->max_inline) { \
        (_desc)->sr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE; \
    } else { \
        (_desc)->sr.send_flags = IBV_SEND_SIGNALED; \
    } \
}

void MV_Send_UD_Normal(
        mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *send_qp, 
        int signalled, int lid) 
{
    mvdev_packet_mheader * mp = (mvdev_packet_mheader *) v->base_ptr;
    mvdev_packet_header * p = (mvdev_packet_header *) v->header_ptr;
    mv_qp *qp = send_qp;

    MV_ASSERT(total_len <= mvparams.mtu - 40);

    SEND_WIN_CHECK(c, v, total_len);

    total_len += MV_HEADER_OFFSET_SIZE; 

    v->seqnum = v->seqnum_last = c->seqnum_next_tosend;
    INCREMENT_SEQ_NUM(&(c->seqnum_next_tosend));
    SET_MHEADER(mp, v->seqnum, 1);

    v->rank = c->global_rank;
    v->segments = v->left_to_send = 1;

    MARK_ACK_COMPLETED(c->global_rank);

    GET_UD_QP(qp, send_qp);
    prepare_ud_descriptor_noheaders( &(v->desc), v->base_ptr, v, total_len,
            c->global_rank, v->seqnum, 1, qp, signalled, lid);

    SEND_UD_SR(qp, v);

    v->rel_type = mvparams.ud_rel ? MV_RELIABLE_FULL : MV_RELIABLE_NONE;

    mvdev_track_send(c, v);
    RESET_CREDITS(v, c);

    if(MVDEV_UNLIKELY(MVDEV_PACKET_R3_DATA == p->type)) {
        ++(c->r3_segments);
    }
    COLLECT_UD_INTERNAL_MSG_INFO(total_len, p->type, c);
}

#define CALC_DESC_COUNT(_total, _chunk_size) (ceil(1.0 * _total / (_chunk_size)))

void MV_Send_UD_Imm(mvdev_connection_t * c, mv_sbuf * v, 
        int total_len, mv_qp *send_qp, int signaled, int lid) 
{

    int len = 0, seqnum, bytes_sent = 0;
    unsigned char * current_pos = v->base_ptr;
    mvdev_packet_header * p = (mvdev_packet_header *) v->header_ptr;
    mv_qp *qp = send_qp;

    SEND_WIN_CHECK(c, v, total_len);

    v->seqnum = c->seqnum_next_tosend;
    v->rank = c->global_rank;
    v->left_to_send = 0;
    v->segments = 0;

    v->desc_chunk = get_sdescriptor_chunk(CALC_DESC_COUNT(total_len, mvparams.mtu - 40));
    v->total_len = total_len;

    do {
        seqnum = v->seqnum_last = c->seqnum_next_tosend;
        INCREMENT_SEQ_NUM(&(c->seqnum_next_tosend));

        len = MIN(mvparams.mtu - 40, total_len - bytes_sent);

        GET_UD_QP(qp, send_qp);

        v->desc_chunk->desc[v->segments].parent = v;
        prepare_ud_descriptor( &(v->desc_chunk->desc[v->segments]), current_pos, v, len,
                c->global_rank, seqnum, (v->segments == 0), qp, signaled, lid);

        SEND_UD_SR_CHUNK(qp, v, v->segments);

        v->rel_type = mvparams.ud_rel ? MV_RELIABLE_FULL : MV_RELIABLE_NONE;
        mvdev_track_send(c, v);

        current_pos += len;
        bytes_sent += len;
        ++(v->segments);
        ++(v->left_to_send);
    } while(bytes_sent < total_len);

    if(MVDEV_UNLIKELY(MVDEV_PACKET_R3_DATA == p->type)) {
        c->r3_segments += v->segments;
    }

    MARK_ACK_COMPLETED(c->global_rank);
    RESET_CREDITS(v, c);

    COLLECT_UD_INTERNAL_MSG_INFO(total_len, p->type, c);
}



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

#ifdef XRC

static inline void prepare_xrc_descriptor_headers(mv_sdescriptor *desc, 
        unsigned char * addr, mv_sbuf *sbuf, unsigned len, mv_qp * qp,
        uint32_t srqn)
{
    desc->sr.next = NULL;

    SET_INLINE(desc, len, qp);

    desc->sr.opcode = IBV_WR_SEND;
    desc->sr.wr_id = (aint_t) desc;
    desc->sr.num_sge = 1;
    desc->sr.sg_list = &(desc->sg_entry);
    desc->sr.xrc_remote_srq_num = srqn;

    desc->qp = qp;
    desc->sg_entry.addr = (uintptr_t) addr;
    desc->sg_entry.length = len;
    desc->sg_entry.lkey = sbuf->buf->region->mem_handle[0]->lkey;
}

void MV_Send_XRC_Normal(mvdev_connection_t * c, mv_sbuf * v, int total_len) {
    int hca = 0;
    mvdev_packet_mheader * mp = (mvdev_packet_mheader *) v->base_ptr;
    /* TODO: support multiple rails */
    mv_qp *qp = &(c->xrc_channel.shared_channel->qp[hca]);
    mvdev_srqn_remote * s = c->xrc_channel.remote_srqn_head;

    total_len += MV_HEADER_OFFSET_SIZE; 

    /* figure out what SRQN we need to send to */
    while(s->buffer_size < total_len) {
        s = s->next;
        MV_ASSERT(s != NULL);
    }

    D_PRINT("Sending over XRC. total_len: %d, srqbuf: %d, srqn: %d\n",
            total_len, s->buffer_size, s->srqn[hca]);

    v->seqnum = v->seqnum_last = c->seqnum_next_tosend;
    INCREMENT_SEQ_NUM(&(c->seqnum_next_tosend));
    SET_MHEADER(mp, v->seqnum, 1);

    v->rank = c->global_rank;
    v->segments = v->left_to_send = 1;
    v->total_len = total_len;

    MARK_ACK_COMPLETED(c->global_rank);

    prepare_xrc_descriptor_headers(&(v->desc), v->base_ptr, v, 
            total_len, qp, s->srqn[hca]);

    /*
    if(mvparams.rc_rel)
        v->rel_type = MV_RELIABLE_FULL_X;
    else
    */
    v->rel_type = MV_RELIABLE_NONE;

    SEND_RC_SR(qp, v);
    RESET_CREDITS(v, c);

    mvdev_track_send(c, v);

#ifdef MV_PROFILE
    {
        mvdev_packet_header * p = (mvdev_packet_header *) v->header_ptr; 
        COLLECT_RC_INTERNAL_MSG_INFO(total_len, p->type, c);
    }
#endif
}


#endif



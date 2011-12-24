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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mv_param.h"
#include "mv_priv.h"
#include "mv_buf.h"
#include "mv_inline.h"

#define CHECK_CH(_c) {   \
    if(NULL == (_c)->channel_rc_fp) {   \
        (_c)->channel_rc_fp = MV_Init_RC_FP(mvparams.rcfp_buf_size); \
    } \
}

mvdev_channel_rc_fp * MV_Init_RC_FP(int buffer_size) 
{
    mvdev_channel_rc_fp * ch = (mvdev_channel_rc_fp *) malloc(sizeof(mvdev_channel_rc_fp));

    ch->buffer_size = buffer_size;
    ch->send_head = 0;
    ch->send_tail = 0;
    ch->recv_head = 0;
    ch->recv_tail = 0;

    ch->send_region = NULL;
    ch->recv_region = NULL;

    /* make the cached values invalid */
    ch->cached_recv.src_lrank = -1;
    ch->cached_send.src_lrank = -1;
    ch->cached_recv.tag = -1;
    ch->cached_send.tag = -1;

    return ch;
}

void MV_Init_Rbuf_Region(mv_rbuf_region *rbuf_region, mv_buf_region *buf_region) 
{
    int i, rev;

    for(i = 0; i < rbuf_region->count; i++) {
        rev = rbuf_region->count - i - 1;
        rbuf_region->array[i].flag = MVDEV_RC_FP;
        rbuf_region->array[i].status = MVDEV_RC_FP_INUSE;
        rbuf_region->array[i].buf = &(buf_region->array[rev]);
        rbuf_region->array[i].base_ptr = buf_region->array[rev].ptr;
        rbuf_region->array[i].header_ptr = 
            rbuf_region->array[i].base_ptr + sizeof(MVBUF_HEAD_FLAG_TYPE);
        rbuf_region->array[i].max_data_size = buf_region->size_ptr->max_data_size;
        rbuf_region->array[i].rpool = NULL;
    }
}

void MV_Init_Sbuf_Region(mvdev_connection_t * c, mv_sbuf_region *sbuf_region, mv_buf_region *buf_region) 
{
    int i, rev;
    int hca = 0;

#ifdef XRC
    mvdev_srqn_remote * s = c->xrc_channel.remote_srqn_head;
#endif
    
    for(i = 0; i < sbuf_region->count; i++) {
        rev = sbuf_region->count - i - 1;
        sbuf_region->array[i].desc.sr.next = NULL;
        sbuf_region->array[i].desc.sr.opcode = IBV_WR_RDMA_WRITE;
        sbuf_region->array[i].desc.sr.wr_id = 
            (aint_t) &(sbuf_region->array[i].desc);
        sbuf_region->array[i].desc.sr.num_sge = 1;
        sbuf_region->array[i].desc.sr.sg_list =
            &(sbuf_region->array[i].desc.sg_entry);
        sbuf_region->array[i].desc.sr.imm_data = 0;

        sbuf_region->array[i].buf = &(buf_region->array[rev]);
        sbuf_region->array[i].max_data_size = buf_region->size_ptr->max_data_size;
        sbuf_region->array[i].base_ptr = buf_region->array[rev].ptr;
        sbuf_region->array[i].desc.sg_entry.addr = 
            (uintptr_t) sbuf_region->array[i].base_ptr;
        sbuf_region->array[i].desc.sg_entry.lkey = 
            sbuf_region->array[i].buf->region->mem_handle[0]->lkey;

        sbuf_region->array[i].header_ptr = 
            sbuf_region->array[i].base_ptr + sizeof(MVBUF_HEAD_FLAG_TYPE);
        sbuf_region->array[i].transport = MVDEV_TRANSPORT_RCFP;
        sbuf_region->array[i].flag = MVDEV_RC_FP;

        sbuf_region->array[i].rel_type = MV_RELIABLE_LOCK_SBUF;
        sbuf_region->array[i].rank = c->global_rank;

        sbuf_region->array[i].in_sendwin = 0;
        sbuf_region->array[i].retry_count = 0;
        sbuf_region->array[i].total_len = 0;
        sbuf_region->array[i].in_progress = 1;
        sbuf_region->array[i].retry_always = 0;


#ifdef XRC
        /* if we are using XRC there are a couple more fields to enable */
        if(c->xrc_enabled) {
             sbuf_region->array[i].desc.sr.xrc_remote_srq_num = s->srqn[hca];
        }
#endif
    }

}

void MV_Init_Conn_RC_FP_Receiver(mvdev_connection_t * c) 
{
    mvdev_channel_rc_fp * ch = c->channel_rc_fp;

    ch->recv_tail = viadev_num_rdma_buffer - 1;
    ch->recv_region = allocate_mv_rbuf_region(viadev_num_rdma_buffer, THREAD_NO);
    ch->recv_buf_region = allocate_mv_buf_region(ch->buffer_size, 
            viadev_num_rdma_buffer, THREAD_NO);
    MV_Init_Rbuf_Region(ch->recv_region, ch->recv_buf_region);
    ch->recv_head_rbuf = &(c->channel_rc_fp->recv_region->array[0]);
    c->rcfp_recv_enabled = 1;
}

void MV_Init_Conn_RC_FP_Sender(mvdev_connection_t * c) 
{
    mvdev_channel_rc_fp * ch = c->channel_rc_fp;

    ch->send_tail = viadev_num_rdma_buffer - 1;
    ch->send_region = allocate_mv_sbuf_region(viadev_num_rdma_buffer, THREAD_NO);
    ch->send_buf_region = allocate_mv_buf_region(ch->buffer_size, 
            viadev_num_rdma_buffer, THREAD_NO);
    MV_Init_Sbuf_Region(c, ch->send_region, ch->send_buf_region);

    D_PRINT("sender array addr: %p\n", &(c->channel_rc_fp->send_region->array[0]));
}

void MV_Send_RC_FP_Addr(mvdev_connection_t * c) 
{
    mv_sbuf * v = get_sbuf(c, sizeof(mvdev_packet_rcfp_addr));
    mvdev_packet_rcfp_addr * p = (mvdev_packet_rcfp_addr *) v->header_ptr;

    PACKET_SET_HEADER(p, c, MVDEV_PACKET_CH_RCFP_ADDR);

    D_PRINT("after header\n", c, v->header_ptr);
    p->rkey = c->channel_rc_fp->recv_buf_region->mem_handle[0]->rkey;
    p->buffer = c->channel_rc_fp->recv_buf_region->data_ptr;

    D_PRINT("sending start buffer: %p\n", p->buffer);

    mvdev_post_channel_send(c, v, sizeof(mvdev_packet_rcfp_addr));
}

void MV_Setup_RC_FP(mvdev_connection_t * c) 
{
    CHECK_CH(c);

    /* first setup on our side (receiving) */
    MV_Init_Conn_RC_FP_Receiver(c);

    /* start polling this buffer for completions */
    mvdev.polling_set[mvdev.polling_set_size++] = c;

    /* send information to other side */
    MV_Send_RC_FP_Addr(c);
}


/* we _SEND_ over FP */
void MV_Recv_RC_FP_Addr(mvdev_connection_t * c,  mvdev_packet_rcfp_addr * p) 
{
    int i;
    mv_sbuf * array;
    char * current_ptr;
    CHECK_CH(c);

    if(!c->rc_enabled && !c->xrc_enabled) {
        c->pending_rcfp = (mvdev_packet_rcfp_addr *) malloc(sizeof(mvdev_packet_rcfp_addr));
        memcpy(c->pending_rcfp, p, sizeof(mvdev_packet_rcfp_addr));
        c->rcfp_send_enabled_xrc_pending = 1;
    } else {
        c->channel_rc_fp->send_rkey = p->rkey;
        c->channel_rc_fp->send_rbuffer = p->buffer;

        MV_Init_Conn_RC_FP_Sender(c);

        /* now init all the sbufs so they have the correct key and address! */
        array = c->channel_rc_fp->send_region->array;
        current_ptr = c->channel_rc_fp->send_rbuffer;
        for(i = viadev_num_rdma_buffer - 1; i >= 0; i--) {
            array[i].desc.sr.wr.rdma.rkey = c->channel_rc_fp->send_rkey;
            array[i].desc.sr.wr.rdma.remote_addr = (uintptr_t) current_ptr;
            current_ptr += mvparams.rcfp_buf_size;
        }

        c->rcfp_send_enabled = 1;
        c->rcfp_send_enabled_xrc_pending = 0;
        c->channel_rc_fp->send_head = 0;

        if(NULL != c->pending_rcfp) {
            free(c->pending_rcfp);
        }
    }
}







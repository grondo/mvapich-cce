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

#ifndef _MV_INLINE_H
#define _MV_INLINE_H

#include <stdio.h>
#include <unistd.h>
#include "mpid.h"
#include "ibverbs_header.h"
#include "mv_dev.h"
#include "mv_priv.h"
#include "mv_buf.h"
#include "mv_packet.h"

#define MARK_ACK_COMPLETED(_rank) {                             \
    mvdev.connection_ack_required[_rank] = 0;                   \
}

#define MARK_ACK_REQUIRED(_rank) {                              \
    mvdev.connection_ack_required[_rank] = 1;                   \
}

static inline void mvdev_post_channel_send_qp_lid(mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *qp, int lid) {
/*        mvdev_func.post_ud_send(c, v, total_len, qp, 1, lid);  */
    MV_ASSERT(v->transport == MVDEV_TRANSPORT_UD);
    MARK_ACK_COMPLETED(c->global_rank);
    if(v->header) {
        MV_Send_UD_Normal(c, v, total_len, qp, 1, lid);
    } else {
        MV_Send_UD_Imm(c, v, total_len, qp, 1, lid);
    }
}

static inline void mvdev_post_channel_send_qp(mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *qp) {
        /* mvdev_func.post_ud_send(c, v, total_len, qp, 1, -1);  */
    MV_ASSERT(v->transport == MVDEV_TRANSPORT_UD);
    MARK_ACK_COMPLETED(c->global_rank);
    if(v->header) {
        MV_Send_UD_Normal(c, v, total_len, qp, 1, -1);
    } else {
        MV_Send_UD_Imm(c, v, total_len, qp, 1, -1);
    }
}

static inline void mvdev_post_channel_send_qp_ns(mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *qp) {
     /*   mvdev_func.post_ud_send(c, v, total_len, qp, 0, -1);  */
    MV_ASSERT(v->transport == MVDEV_TRANSPORT_UD);
    MARK_ACK_COMPLETED(c->global_rank);
    if(v->header) {
        MV_Send_UD_Normal(c, v, total_len, qp, 0, -1);
    } else {
        MV_Send_UD_Imm(c, v, total_len, qp, 0, -1);
    }
}

static inline void mvdev_post_channel_send(mvdev_connection_t * c, mv_sbuf * v, int total_len) {
    mvdev_channel_rc * h = c->rc_channel_head;
    MARK_ACK_COMPLETED(c->global_rank);
    switch(v->transport) {
        case MVDEV_TRANSPORT_RC:
            while(NULL != h) {
                if(h->qp[0].max_send_size >= (total_len + MV_HEADER_OFFSET_SIZE) || NULL == h->next) {
                    break;
                }   
                h = h->next;
            } 

            if(v->header) {
                MV_Send_RC_Normal(c, v, total_len, &(h->qp[0]));
            } else {
                MV_Send_RC_Imm(c, v, total_len, &(h->qp[0]));
            }
            break; 
        case MVDEV_TRANSPORT_UD:
            if(v->header) {
                MV_Send_UD_Normal(c, v, total_len, NULL, 1, -1);
            } else {
                MV_Send_UD_Imm(c, v, total_len, NULL, 1, -1);
            }
            break;
        case MVDEV_TRANSPORT_RCFP:
#ifdef XRC
            if(c->xrc_enabled) {
                MV_Send_RCFP_Normal(c, v, total_len, &(c->xrc_channel.shared_channel->qp[0]));
            } else 
#endif
            {
                MV_Send_RCFP_Normal(c, v, total_len, &(h->qp[0]));
            }
            break;
#ifdef XRC
        case MVDEV_TRANSPORT_XRC:
            {
                MV_ASSERT(v->header);
                MV_Send_XRC_Normal(c, v, total_len);
                break;
            }
#endif
        default:
            error_abort_all(IBV_RETURN_ERR,"Invalid transport %d\n",
                  v->transport);
    }
}


void mvdev_windowq_queue(mvdev_connection_t * c, mv_sbuf * v, int total_len);
void mvdev_ext_sendq_queue(mv_qp *qp, mv_sdescriptor * d);
void mvdev_ext_backlogq_queue(mv_qp *qp, mv_sdescriptor * d);

#define SET_MHEADER(_mp, _seqnum, _hasheader) {        \
    (_mp)->has_header = _hasheader;                    \
    (_mp)->seqnum = _seqnum;                           \
    (_mp)->src_rank = mvdev.me;                        \
}

static inline void prepare_ud_descriptor_noheaders(mv_sdescriptor *desc, unsigned char * addr,
        mv_sbuf *sbuf,
        unsigned len, int peer, uint32_t seqnum, uint32_t has_header,
        mv_qp * qp, int last, int ah_index)
{
    desc->sr.next = NULL;
    if(len <= qp->max_inline) {
        desc->sr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE; 
    } else {
        desc->sr.send_flags = IBV_SEND_SIGNALED;
    }
    desc->sr.opcode = IBV_WR_SEND;
    desc->sr.wr_id = (aint_t) desc;
    desc->sr.num_sge = 1;
    desc->sr.sg_list = &(desc->sg_entry);

    if(ah_index != -1) {
        desc->sr.wr.ud.ah = mvdev.connections[peer].data_ud_ah[ah_index];
    } else {
        desc->sr.wr.ud.ah = mvdev.connections[peer].data_ud_ah[mvdev.connections[peer].last_ah];
        mvdev.connections[peer].last_ah = 
            (mvdev.connections[peer].last_ah + 1) % mvparams.max_ah_total;
    }
    desc->sr.wr.ud.remote_qpn = mvdev.qpns[peer];
    desc->sr.wr.ud.remote_qkey = 0;

    desc->sg_entry.addr = (uintptr_t) addr;
    desc->sg_entry.length = len;
    desc->sg_entry.lkey = sbuf->buf->region->mem_handle[0]->lkey;

    desc->qp = qp;
}
static inline void prepare_ud_descriptor(mv_sdescriptor *desc, unsigned char * addr,
        mv_sbuf *sbuf,
        unsigned len, int peer, uint32_t seqnum, uint32_t has_header,
        mv_qp * qp, int last, int ah_index)
{
    desc->sr.next = NULL;
    if(MVDEV_LIKELY(last)) {
        if(len <= qp->max_inline) {
            desc->sr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE; 
        } else {
            desc->sr.send_flags = IBV_SEND_SIGNALED;
        }
    } else {
        if(len <= mvparams.ud_max_inline) {
            desc->sr.send_flags = IBV_SEND_INLINE; 
        } else {
            desc->sr.send_flags = 0;
        }

    }
    desc->sr.opcode = IBV_WR_SEND_WITH_IMM;
    desc->sr.wr_id = (aint_t) desc;
    desc->sr.num_sge = 1;
    desc->sr.sg_list = &(desc->sg_entry);

    desc->sr.imm_data = CREATE_THS_TUPLE(has_header, mvdev.me, seqnum);

    if(ah_index != -1) {
        desc->sr.wr.ud.ah = mvdev.connections[peer].data_ud_ah[ah_index];
    } else {
        desc->sr.wr.ud.ah = mvdev.connections[peer].data_ud_ah[mvdev.connections[peer].last_ah];
        mvdev.connections[peer].last_ah = 
            (mvdev.connections[peer].last_ah + 1) % mvparams.max_ah_total;
    }

    desc->sr.wr.ud.remote_qpn = mvdev.qpns[peer];
    desc->sr.wr.ud.remote_qkey = 0;

    desc->sg_entry.addr = (uintptr_t) addr;
    desc->sg_entry.length = len;
    desc->sg_entry.lkey = sbuf->buf->region->mem_handle[0]->lkey;

    desc->qp = qp;
}

static inline void xprepare_rc_descriptor_headers(mv_sdescriptor *desc,  
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



static inline void mvdev_post_channel_send_control(mvdev_connection_t * c, 
        mv_sbuf * v, unsigned total_len) {
    struct ibv_send_wr *bad_wr;
    mvdev_packet_mheader * mp = (mvdev_packet_mheader *) v->base_ptr;
    SET_MHEADER(mp, 0, 1);

    v->seqnum = v->seqnum_last = 0;
    v->segments = v->left_to_send = 1;

    total_len += MV_HEADER_OFFSET_SIZE;

    MARK_ACK_COMPLETED(c->global_rank);

    /* TODO: controls can be send over either! */
    if(MVDEV_TRANSPORT_UD == v->transport) {
        mvdev.last_qp = (mvdev.last_qp + 1) % mvdev.num_ud_qps;

        prepare_ud_descriptor_noheaders( &(v->desc), v->base_ptr,
                v, total_len, c->global_rank, 
                v->seqnum, 1, &(mvdev.ud_qp[mvdev.last_qp]), 1, -1);

        if(mvdev.ud_qp[mvdev.last_qp].send_wqes_avail <= 0) {
            mvdev_ext_sendq_queue(&(mvdev.ud_qp[mvdev.last_qp]), &(v->desc));
        } else {
            mvdev.ud_qp[mvdev.last_qp].send_wqes_avail--;

            if(ibv_post_send(mvdev.ud_qp[mvdev.last_qp].qp, 
                        &(v->desc.sr), &bad_wr)) {
                error_abort_all(IBV_RETURN_ERR,"Error posting to UD QP %d\n",
                        mvdev.ud_qp[mvdev.last_qp].send_wqes_avail);
            }
        }
        COLLECT_UD_INTERNAL_MSG_INFO(total_len, MVDEV_PACKET_ACK, c);
    } else if(MVDEV_TRANSPORT_RC == v->transport) {
        mvdev_channel_rc * uc_h = c->rc_channel_head;
        mv_qp *qp = &(uc_h->qp[0]);
        xprepare_rc_descriptor_headers(&(v->desc), v->base_ptr, v, 
                total_len, qp);
        SEND_RC_SR(qp, v);
        COLLECT_RC_INTERNAL_MSG_INFO(total_len, MVDEV_PACKET_ACK, c);
    } else {
        error_abort_all(IBV_RETURN_ERR,"Invalid transport for control %d\n",
            v->transport);
    }

    RESET_CREDITS(v, c);

}

/* we only add to the end of the sendwin, making this a bit easier... */

static inline void mvdev_sendwin_add(mvdev_connection_t * c, mv_sbuf *ptr)  {
    ptr->sendwin_ptr.next = ptr->sendwin_ptr.prev = NULL;

    ptr->in_sendwin = 1;

    if(NULL == c->send_window_head) {
        c->send_window_head = ptr;
    } else {
        (c->send_window_tail)->sendwin_ptr.next = ptr;
    }

    c->send_window_tail = ptr;
    ++(c->send_window_size);
}

static inline void mvdev_unackq_add(mv_sbuf *ptr)  {
    ptr->unackq_ptr.next = NULL; 

    if(NULL == mvdev.unack_queue_head) {
        mvdev.unack_queue_head = ptr;
        ptr->unackq_ptr.prev = NULL; 
    } else {
        mvdev.unack_queue_tail->unackq_ptr.next = ptr;
        ptr->unackq_ptr.prev = mvdev.unack_queue_tail;
    }

    mvdev.unack_queue_tail = ptr;
}

static inline void mvdev_track_send(mvdev_connection_t * c, mv_sbuf * v) {

    if(v->rel_type != MV_RELIABLE_NONE) {
        ++(c->send_window_segments);
    }

    mvdev.last_check = get_time_us();

    if(v->in_sendwin) {
        return;
    }

    switch(v->rel_type) {
        case MV_RELIABLE_LOCK_SBUF:
        case MV_RELIABLE_FREE_SBUF:
            mvdev_sendwin_add(c, v);
            break;
        case MV_RELIABLE_FULL_X:
        case MV_RELIABLE_FULL:
            v->timestamp = get_time_us();
            mvdev_sendwin_add(c, v);
            mvdev_unackq_add(v);

#ifdef MV_PROFILE
            c->msg_info.send_window_bytes += v->max_data_size;
            mvdev.msg_info.send_window_bytes += v->max_data_size;
            if(c->msg_info.send_window_bytes > 
                    c->msg_info.max_send_window_bytes) {
                c->msg_info.max_send_window_bytes =
                    c->msg_info.send_window_bytes;
            }
            if(mvdev.msg_info.send_window_bytes > 
                    mvdev.msg_info.max_send_window_bytes) {
                mvdev.msg_info.max_send_window_bytes =
                    mvdev.msg_info.send_window_bytes;
            }
            c->msg_info.send_entries += v->segments;
            mvdev.msg_info.send_entries += v->segments;
            mvdev.msg_info.max_send_entries = 
                MAX(mvdev.msg_info.max_send_entries, mvdev.msg_info.send_entries);
            c->msg_info.max_send_entries = 
                MAX(c->msg_info.max_send_entries, c->msg_info.send_entries);
#endif
            break;
        case MV_RELIABLE_NONE:
            break;
    }

}

/* we can remove from anywhere within the unackq */
static inline void mvdev_unackq_remove(mv_sbuf *ptr)  {
    mv_sbuf * prev_ptr = (mv_sbuf *) ptr->unackq_ptr.prev;
    mv_sbuf * next_ptr = (mv_sbuf *) ptr->unackq_ptr.next;

    if(NULL == prev_ptr) {
        mvdev.unack_queue_head = next_ptr;
    } else {
        prev_ptr->unackq_ptr.next = next_ptr;
    }

    if(NULL == next_ptr) {
        mvdev.unack_queue_tail = prev_ptr;
    } else {
        next_ptr->unackq_ptr.prev = prev_ptr;
    }
}

static inline void mvdev_unackq_traverse() {
    int delay;
    double timestamp = get_time_us();
    mv_sbuf * current = mvdev.unack_queue_head;

    while(NULL != current) {
        if(0 == current->left_to_send || current->retry_always) {
            delay = timestamp - current->timestamp;

            if(delay > mvparams.retry_usec) {
                current->timestamp = timestamp;
                D_PRINT("resending seq: %d\n", current->seqnum);
                mvdev_resend(current);
                timestamp = get_time_us();
            }
        }

        current = current->unackq_ptr.next;
    }
}

void mvdev_ext_sendq_send(mv_qp * qp);
void mvdev_ext_backlogq_send(mv_qp * qp);

/* we can add anywhere in the recvq, so we need more complex logic
 * here
 */

static inline void mvdev_recvwin_add(mvdev_connection_t * c, mv_rbuf *ptr)  {
    ptr->recvwin_ptr.next = ptr->recvwin_ptr.prev = NULL;

    if(MVDEV_UNLIKELY(NULL == c->recv_window_head)) {
        c->recv_window_head = ptr;
        c->recv_window_tail = ptr;
    } else {
        mv_rbuf * curr_rbuf = c->recv_window_head;

        if(MVDEV_LIKELY(ptr->seqnum > c->recv_window_start_seq)) {
            if(curr_rbuf->seqnum < c->recv_window_start_seq) {
            } 
            else {
                while(NULL != curr_rbuf && curr_rbuf->seqnum < ptr->seqnum
                        && curr_rbuf->seqnum > c->recv_window_start_seq) {
                    curr_rbuf = curr_rbuf->recvwin_ptr.next;
                }
            }
        } else {
            if(MVDEV_UNLIKELY(curr_rbuf->seqnum > c->recv_window_start_seq)) {
                while(NULL != curr_rbuf &&
                        ((curr_rbuf->seqnum >= c->recv_window_start_seq) ||
                         (curr_rbuf->seqnum < ptr->seqnum))) {
                    curr_rbuf = curr_rbuf->recvwin_ptr.next;
                }
            } else {
                while(NULL != curr_rbuf && curr_rbuf->seqnum < ptr->seqnum) {
                    curr_rbuf = curr_rbuf->recvwin_ptr.next;
                }
            }
        }

        if(MVDEV_LIKELY(NULL != curr_rbuf)) {
            if(curr_rbuf->seqnum == ptr->seqnum) {
                return; /* a bit hacky to just return, but... */
            }

            ptr->recvwin_ptr.next = curr_rbuf;
            ptr->recvwin_ptr.prev = curr_rbuf->recvwin_ptr.prev;

            if(curr_rbuf == c->recv_window_head) {
                c->recv_window_head = ptr;
            } else {
                ((mv_rbuf *) curr_rbuf->recvwin_ptr.prev)->recvwin_ptr.next = ptr;
            }
            curr_rbuf->recvwin_ptr.prev = ptr;
        } else {
            ptr->recvwin_ptr.next = NULL;
            ptr->recvwin_ptr.prev = c->recv_window_tail;
            c->recv_window_tail->recvwin_ptr.next = ptr;
            c->recv_window_tail = ptr;
        }
    }
    ++(c->recv_window_size);
}

void mvdev_windowq_send(mvdev_connection_t * c);

static inline void mvdev_sendwin_remove(mvdev_connection_t * c, mv_sbuf *ptr)  {
    mv_sbuf * next_ptr = (mv_sbuf *) ptr->sendwin_ptr.next;

    ptr->in_sendwin = 0;

    c->send_window_head = next_ptr;
    c->send_window_size--;
    c->send_window_segments -= ptr->segments;

    if(NULL == next_ptr) {
        c->send_window_tail = NULL;
    }

    /* see if we can flush anything from the window queue */
    if(NULL != c->ext_window_head &&
            c->send_window_segments < mvparams.send_window_segments_max) {
        mvdev_windowq_send(c);
    } 

    if(c->blocked_count > 0) {
        PUSH_FLOWLIST(c);
    }
}

static inline void mvdev_recvwin_remove(mvdev_connection_t * c, mv_rbuf *ptr)  {
    mv_rbuf * next_ptr = (mv_rbuf *) ptr->recvwin_ptr.next;

    c->recv_window_head = next_ptr;
    if(next_ptr != NULL) {
        next_ptr->recvwin_ptr.prev = NULL;
    } else {
        c->recv_window_tail = NULL;
        c->recv_window_head = NULL;
    }

    c->recv_window_size--;
}


/* Figure out where in the list we need to add this packet. 
 * If we are the first entry then there is no need to queue the packet
 * and it can just be processed. That will be the quickest path
 */

static inline void mvdev_place_recvwin(mvdev_connection_t * c, mv_rbuf * v) {
    packet_sequence_t win_start = c->recv_window_start_seq;
    packet_sequence_t win_end   =
        (c->recv_window_start_seq + mvparams.recv_window_size_max) % MAX_SEQ_NUM;

    /* check if the packet is in the window or not */
    if(MVDEV_LIKELY(INCL_BETWEEN(v->seqnum, win_start, win_end))) {
        if(MVDEV_LIKELY(v->seqnum == c->recv_window_start_seq)) {
            D_PRINT("[%d] Got exact in-order recv (got %d)\n", c->global_rank, v->seqnum);

            /* we are next in the queue to be processes */
            mvdev_process_recv_inorder(c, v);

            switch(v->transport) {
                case MVDEV_TRANSPORT_UD:
                    if(mvparams.ud_rel)
                        MARK_ACK_REQUIRED(c->global_rank);
                    break;
                case MVDEV_TRANSPORT_RC:
                    if(mvparams.rc_rel)
                        MARK_ACK_REQUIRED(c->global_rank);
                    break;
                case MVDEV_TRANSPORT_XRC:
                    if(mvparams.rc_rel)
                        MARK_ACK_REQUIRED(c->global_rank);
                    break;
            }

            c->seqnum_next_toack = v->seqnum;
            INCREMENT_SEQ_NUM(&(c->recv_window_start_seq));
        }
        else {
            /* we are not in order... */
            D_PRINT("Got out-of-order recv (got %d, exp: %d)\n", 
                    v->seqnum, win_start);
            mvdev_recvwin_add(c, v);
            if(MVDEV_TRANSPORT_UD == v->transport) {
                if(mvparams.ud_rel) {
                    MARK_ACK_REQUIRED(c->global_rank);
                }
            }
        }

        while(NULL != c->recv_window_head &&
                PEEK_RECV_WIN(c) == c->recv_window_start_seq) {
            int current_seqnum = PEEK_RECV_WIN(c);
            mvdev_process_recv_inorder(c, c->recv_window_head);
            mvdev_recvwin_remove(c, c->recv_window_head);
            c->seqnum_next_toack = current_seqnum;

            INCREMENT_SEQ_NUM(&(c->recv_window_start_seq));
        }
    } else {
        D_PRINT("Message is not in window, seq: %d, start: %d, end: %d\n", 
                v->seqnum, win_start, win_end);
        release_mv_rbuf(v);
        if(MVDEV_TRANSPORT_UD == v->transport) {
            if(mvparams.ud_rel) {
                MARK_ACK_REQUIRED(c->global_rank);
            }
        }
    }
}

static inline void mvdev_process_ack(mvdev_connection_t *c, uint32_t last_recv)
{
    D_PRINT("Last recv'd pkt (from remote): %d, last acked: %d\n",
            last_recv, c->seqnum_next_toack);
    mv_sbuf * head_ptr = c->send_window_head;

    while(NULL != head_ptr &&
            INCL_BETWEEN(last_recv, head_ptr->seqnum, c->seqnum_next_tosend)) {
        if(MVDEV_LIKELY(INCL_BETWEEN(head_ptr->seqnum_last, 
                        head_ptr->seqnum, last_recv))) {
            mvdev_sendwin_remove(c, head_ptr);

            switch(head_ptr->rel_type) {
                case MV_RELIABLE_FULL:
                    mvdev_unackq_remove(head_ptr);
                    mvdev_process_send_afterack(head_ptr);

#ifdef MV_PROFILE
                    c->msg_info.send_window_bytes -= head_ptr->max_data_size;
                    mvdev.msg_info.send_window_bytes -= head_ptr->max_data_size;
                    c->msg_info.send_entries -= head_ptr->segments;
                    mvdev.msg_info.send_entries -= head_ptr->segments;
#endif

                    if(MVDEV_UNLIKELY(head_ptr->in_progress)) {
                        head_ptr->seqnum = 0;
                    } else {
                        release_mv_sbuf(head_ptr);
                    }
                    break;
                case MV_RELIABLE_FULL_X:
                    {
                        mv_qp * qp;

                        mvdev_unackq_remove(head_ptr);
                        mvdev_process_send_afterack(head_ptr);

                        if(NULL == head_ptr->desc_chunk) {
                            qp = (mv_qp *) head_ptr->desc.qp;
                        } else {
                            qp = (mv_qp *) head_ptr->desc_chunk->desc[0].qp;
                        }

                        qp->send_credits_remaining += head_ptr->segments;
                        if(MVDEV_UNLIKELY(NULL != qp->ext_backlogq_head)) {
                            mvdev_ext_backlogq_send(qp); 
                        }

#ifdef MV_PROFILE
                        c->msg_info.send_window_bytes -= head_ptr->max_data_size;
                        mvdev.msg_info.send_window_bytes -= head_ptr->max_data_size;
                        c->msg_info.send_entries -= head_ptr->segments;
                        mvdev.msg_info.send_entries -= head_ptr->segments;
#endif

                        if(head_ptr->in_progress) {
                            head_ptr->seqnum = 0;
                        } else {
                            release_mv_sbuf(head_ptr);
                        }
                        break;
                    }

                case MV_RELIABLE_FREE_SBUF:
                    {
                        mv_qp * qp;
                        if(NULL == head_ptr->desc_chunk) {
                            qp = (mv_qp *) head_ptr->desc.qp;
                        } else {
                            qp = (mv_qp *) head_ptr->desc_chunk->desc[0].qp;
                        }

                        qp->send_credits_remaining += head_ptr->segments;
                        D_PRINT("credits +%d to %d\n", head_ptr->segments, qp->send_credits_remaining);
                        if(MVDEV_UNLIKELY(NULL != qp->ext_backlogq_head)) {
                            mvdev_ext_backlogq_send(qp); 
                        }

                        if(head_ptr->in_progress) {
                            mvdev_process_send_afterack(head_ptr);
                            head_ptr->seqnum = 0;
                        } else {
                            release_mv_sbuf(head_ptr);
                        }
                        break;
                    }
                case MV_RELIABLE_LOCK_SBUF:
                    if(MVDEV_LIKELY(MVDEV_TRANSPORT_RCFP == head_ptr->transport)) {
                        if(head_ptr->in_progress) {
                            head_ptr->seqnum = 0;
                            if(head_ptr->header != 2) {
                                mvdev_process_send_afterack(head_ptr);
                            } else {
                                head_ptr->header = 1;
                            }
                        } else {
                            release_mv_sbuf(head_ptr);
                        }
                    }
                    break;
            }


            head_ptr = c->send_window_head;
        } 
        else {
            /* only got a partial message acknowledgement, we
             * can try to do more intelligent things here. e.g.
             * it is somewhat likely the end of the message got dropped
             */
            D_PRINT("Got partial!\n");
            break;
        }
    }
}



static inline void mvdev_explicit_ack(int rank) {
    mvdev_connection_t *c = &(mvdev.connections[rank]);
    mv_sbuf *v = get_sbuf_control(c, sizeof(mvdev_packet_basic));
    mvdev_packet_basic * p = (mvdev_packet_basic *) v->header_ptr;

    MV_ASSERT(rank != mvdev.me);

    PACKET_SET_HEADER(p, c, MVDEV_PACKET_ACK);
    mvdev_post_channel_send_control(c, v, sizeof(mvdev_packet_basic));

#ifdef MV_PROFILE
    c->msg_info.control_all++;
#endif

    MV_ASSERT(p->header.type == MVDEV_PACKET_ACK);

    D_PRINT("Sending explict ACK to %d\n", rank);
}



#define SEND_ACKS() { \
    int ack_i; \
    for(ack_i = 0; ack_i < mvdev.np; ++ack_i) { \
        if(mvdev.connection_ack_required[ack_i]) { \
            mvdev.connections[ack_i].msg_info.control_ack++; \
            mvdev_explicit_ack(ack_i); \
        }       \
    } \
}



#endif

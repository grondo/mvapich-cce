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
#include "ibverbs_header.h"
#include "mv_priv.h"
#include "mv_packet.h"
#include "queue.h"
#include "mv_param.h"
#include "mpid_smpi.h"
#include "mv_buf.h"
#include "mv_inline.h"
#include "dreg.h"

#if defined(_IA64_)
   #if defined(__INTEL_COMPILER)
      #define RMB()  __mf();
   #else
      #define RMB()  asm volatile ("mf" ::: "memory");
   #endif
#elif defined(_IA32_)
      #define RMB()  asm volatile ("lfence" ::: "memory");
#elif defined(_X86_64_)
      #define RMB()  asm volatile ("lfence" ::: "memory");
#elif defined(_EM64T_)
      #define RMB()  asm volatile ("lfence" ::: "memory");
#elif defined(_PPC64_)
      #define RMB()  asm volatile ("sync": : :"memory");
#else
      #define RMB()
#endif                          /* defined(_IA64_ ... */

#define SET_BYTE_LEN_HEADER(_v, _wc) {                                           \
    if(MVDEV_TRANSPORT_UD == _v->transport) {                                    \
        _v->byte_len_data = _v->byte_len = _wc.byte_len - 40 - MV_HEADER_OFFSET_SIZE; \
        _v->byte_len_full = _wc.byte_len - 40; \
    } else {                                                                     \
        _v->byte_len_data = _v->byte_len = _wc.byte_len - MV_HEADER_OFFSET_SIZE;      \
        _v->byte_len_full = _wc.byte_len;      \
    }                                                                            \
}

#define SET_BYTE_LEN_IMM(_v, _wc) {                                              \
    if(MVDEV_TRANSPORT_UD == _v->transport) {                                    \
        _v->byte_len_data = _v->byte_len = _v->byte_len_full = _wc.byte_len - 40;   \
    } else {                                                                     \
        _v->byte_len_data = _v->byte_len = _v->byte_len_full = _wc.byte_len;     \
    }                                                                            \
}
#define SET_RECV_IMM(_v) {                                              \
    _v->header = 0; \
    _v->header_ptr = _v->base_ptr + MV_TRANSPORT_OFFSET(_v); \
}
#define SET_RECV_HEADER(_v) {                                              \
    _v->header = 1; \
    _v->header_ptr = _v->base_ptr + MV_TRANSPORT_OFFSET(_v) + MV_HEADER_OFFSET_SIZE; \
}

int mvdev_process_send(mv_sdescriptor *d, mv_sbuf * v) 
{
    mv_qp *qp = (mv_qp *) d->qp;

    ++(qp->send_wqes_avail);
    if(NULL != qp->ext_sendq_head && qp->send_wqes_avail > 0) {
        mvdev_ext_sendq_send(qp);
    } 

    if(--(v->left_to_send) == 0) {
        v->in_progress = 0;

        /* immediately free packets that are unacknowledged */
        if(0 == v->seqnum) { 
            release_mv_sbuf(v);
        } else {
            /* if we sent it over RC a send completion means that the
             * we can process the send operation immediately
             */
            switch(v->rel_type) {
                case MV_RELIABLE_NONE:
                    mvdev_process_send_afterack(v);
                    release_mv_sbuf(v);
                    break;
                case MV_RELIABLE_LOCK_SBUF:
                    if(v->header != 2) {
                        mvdev_process_send_afterack(v);
                    } else {
                        v->header = 1;
                    }
                    break;
                case MV_RELIABLE_FREE_SBUF:
                    if(v->in_sendwin) {
                        mvdev_process_send_afterack(v);
                        release_mv_buf_s(v);
                    } else {
                        release_mv_sbuf(v);
                    }
                    break;
                case MV_RELIABLE_FULL:
                case MV_RELIABLE_FULL_X:
                    break;
                default:
                    error_abort_all(IBV_STATUS_ERR, "Unknown rel type %d "
                            "in process send", v->rel_type); 

            }
        }
    } 

    return 0;
}


int mvdev_process_recv(mv_rbuf * v)
{
    mvdev_connection_t *c = &(mvdev.connections[v->rank]);

    if(v->rpool) {
        DECR_RPOOL(((mv_rpool *) (v->rpool)));
        CHECK_RPOOL(((mv_rpool *) (v->rpool)));
    }

    if(v->has_header) {

        /* is this something that could have gone over rcfp ? */
        if(v->byte_len < 2048 && !c->rcfp_recv_enabled && 
                ++(c->rcfp_messages) > mvparams.rcfp_threshold && (c->rc_enabled || c->xrc_enabled) &&
                mvdev.rcfp_connections < mvparams.max_rcfp_connections) {
            ++(mvdev.rcfp_connections);
            MV_Setup_RC_FP(c); 
        }

        mvdev_packet_header *p = (mvdev_packet_header *) v->header_ptr;
        mvdev_process_ack(c, p->last_recv);

        switch(p->type) {
            case MVDEV_PACKET_EAGER_START:
                MV_SBUF_SET_DATA(v, mvdev_packet_eager_start);
                break;
            case MVDEV_PACKET_EAGER_NEXT:
                MV_SBUF_SET_DATA(v, mvdev_packet_eager_next);
                break;
            case MVDEV_PACKET_R3_DATA:
                MV_SBUF_SET_DATA(v, mvdev_packet_r3_data);
                break;
        }

        switch(p->type) {
            case MVDEV_PACKET_ACK:
                MV_ASSERT(v->seqnum == 0);
                release_mv_rbuf(v);
                break;  
            default:
                MV_ASSERT(v->seqnum != 0);
                ACK_CREDIT_CHECK(c, v);
#ifdef MV_PROFILE
                c->msg_info.total_recv_bytes += v->byte_len_full;
                c->msg_info.total_recvb_bytes += v->max_data_size;
#endif
                mvdev_place_recvwin(c, v);
        }       
    } else {
        ACK_CREDIT_CHECK(c, v);
        mvdev_place_recvwin(c, v);
    }
    return 0;
}

void mvdev_process_send_afterack(mv_sbuf * v) 
{
    mvdev_packet_header *p;

    /* if this is an rput vbuf then release! */
    if(MVDEV_RPUT_FLAG == v->flag) {
        return;
    }

    p = (mvdev_packet_header *) v->header_ptr;

    switch(p->type) {
        case MVDEV_PACKET_EAGER_START:
        case MVDEV_PACKET_EAGER_NEXT:
        case MVDEV_PACKET_RENDEZVOUS_START:
        case MVDEV_PACKET_RENDEZVOUS_REPLY:
        case MVDEV_PACKET_UD_ZCOPY_FINISH:
        case MVDEV_PACKET_UD_ZCOPY_ACK:
        case MVDEV_PACKET_CH_REQUEST:
        case MVDEV_PACKET_CH_REPLY:
        case MVDEV_PACKET_CH_ACK:
        case MVDEV_PACKET_CH_RCFP_ADDR:
        case MVDEV_PACKET_RPUT_ACK:
            goto release_vbuf;
            break;  
        case MVDEV_PACKET_RPUT_FINISH:
            {
                MPIR_SHANDLE * s = (MPIR_SHANDLE *) (v->shandle);
                /* if we are using a reliable transport then send completion
                 * we can free the buffer
                 */
                if(s != NULL && MVDEV_PROTOCOL_REL_RPUT == s->protocol) {
                    SEND_COMPLETE(s);
                }
                goto release_vbuf;
                break;
            }
        case MVDEV_PACKET_R3_DATA:
            {
                mvdev_connection_t *c = &(mvdev.connections[v->rank]);
                c->r3_segments -= v->segments;

                if(c->r3_segments < mvparams.r3_segments_max && c->blocked_count > 0) {
                    PUSH_FLOWLIST(c);
                }
                break;
            }
        default:
            error_abort_all(IBV_STATUS_ERR, "Unknown packet type %d "
                    " id: %d "
                    "in mvdev_process_send_afterack", p->type, v->seqnum); 
    }

release_vbuf:
    return;
}

static inline void mvdev_recv_fast_eager(mv_rbuf * v, mvdev_connection_t * c) 
{
    MPIR_RHANDLE *rhandle;
    int found, copy_bytes;

    MPID_Msg_arrived(c->channel_rc_fp->cached_recv.src_lrank, c->channel_rc_fp->cached_recv.tag, 
            c->channel_rc_fp->cached_recv.context_id, &rhandle, &found);

    if(MVDEV_LIKELY(found)) {
        if(MVDEV_LIKELY(0 < (copy_bytes = MIN(rhandle->len, v->byte_len_data)))) {
            memcpy((char *) (rhandle->buf), v->data_ptr, copy_bytes);
        }
        rhandle->s.MPI_ERROR = MPI_SUCCESS;
        rhandle->len = rhandle->s.count = v->byte_len_data;
        RECV_COMPLETE(rhandle);
    } else {
        mv_buf * buf = MV_Copy_Unexpected_Rbuf(v);

        rhandle->connection = c;
        rhandle->s.count = v->byte_len_data;
        rhandle->protocol = MVDEV_PROTOCOL_EAGER;
        rhandle->vbufs_received = 1;
        rhandle->len = v->byte_len_data;
        rhandle->vbuf_head = rhandle->vbuf_tail = buf;
        rhandle->bytes_copied_to_user = 0;
        c->rhandle = rhandle; 
    }
}

static inline int mvdev_check_rcfp(mvdev_connection_t * c) 
{
    int empty = 0;

    do {

    mvdev_channel_rc_fp * ch = c->channel_rc_fp;
    mv_rbuf * v = ch->recv_head_rbuf;
    volatile MVBUF_HEAD_FLAG_TYPE *head;
    volatile MVBUF_TAIL_FLAG_TYPE *tail;

    head = (MVBUF_HEAD_FLAG_TYPE *) v->base_ptr;

    if(0 != *head) {
        uint64_t head_val, tail_val, size, seq, type, last_recv;

        READ_RCFP_HEADER(*head, (&head_val), (&size), (&seq));
        tail = (MVBUF_TAIL_FLAG_TYPE *) (v->base_ptr + size - sizeof(MVBUF_TAIL_FLAG_TYPE));
        READ_RCFP_FOOTER(*tail, (&tail_val), (&type), (&last_recv));

        if(MVDEV_LIKELY(tail_val == head_val)) {
#ifndef DISABLE_RMB
            RMB();
#endif
            READ_RCFP_HEADER(*head, (&head_val), (&size), (&seq));

            v->byte_len_data = v->byte_len = size - MV_RCFP_OVERHEAD;
            v->has_header = 1;
            v->rank       = c->global_rank;
            v->seqnum     = seq;
            v->transport  = MVDEV_TRANSPORT_RCFP;

            *head = 0;
            if(MVDEV_UNLIKELY(++(ch->recv_head) >= viadev_num_rdma_buffer)) {
                ch->recv_head = 0;
            }

            ch->recv_head_rbuf = &(ch->recv_region->array[ch->recv_head]);

            if(MVDEV_LIKELY(MVDEV_PACKET_FP_CACHED_EAGER == type)) {
                v->data_ptr = v->base_ptr + sizeof(MVBUF_HEAD_FLAG_TYPE);

                /* need special processing for a cached eager */
                mvdev_process_ack(c, last_recv);

                /* special code to know it is not normal */
                v->has_header = 2;

                /* take a processing shortcut if we are the next packet
                 * and there are no others waiting 
                 */
                if(MVDEV_LIKELY(NULL == c->recv_window_head && v->seqnum == c->recv_window_start_seq)) {
                    mvdev_recv_fast_eager(v, c);
                    c->seqnum_next_toack = v->seqnum;
                    INCREMENT_SEQ_NUM(&(c->recv_window_start_seq));
                    release_mv_rbuf(v);
                } else {
                    mvdev_place_recvwin(c, v);
                }
                ACK_CREDIT_CHECK(c, v);
            } else {
                mvdev_process_recv(v);
            }

            return 1;
        }
    } else {
        empty = 1;
    }
    } while (!empty);

    return 0;
}

void MV_Poll_CQ_Error(struct ibv_wc * wc) 
{
    if(IBV_WC_SEND == wc->opcode) { 
        mv_sdescriptor *d = (mv_sdescriptor *) wc->wr_id;
        mv_sbuf * v = ((mv_sbuf *)(d->parent));
        error_abort_all(IBV_STATUS_ERR, "got completion with "
                "error. code: %d, wr_id: %lu, maxlen: %d, s: %d, qpn: %d\n", 
                wc->status, wc->wr_id, v->max_data_size, 
                v->total_len, ((mv_qp *)d->qp)->remote_qpn);
    } else if(IBV_WC_RECV == wc->opcode) {
        error_abort_all(IBV_STATUS_ERR, "got completion with error. "
                "code: %d, wr_id: %lu\n", wc->status, wc->wr_id);
    } else {
        error_abort_all(IBV_STATUS_ERR, "got completion with "
                "error. code: %d, wr_id: %lu\n", wc->status, wc->wr_id);
    }

}


int MPID_DeviceCheck(MPID_BLOCKING_TYPE blocking)
{
    static int last_polled = 0;
    int ne = 0, progress = 0, count = 0;
    int empty = 1, i, j;
    struct ibv_wc wc;
    static int recv = 0;

    do {    

        /* Poll SMP */
#ifdef _SMP_
        if (!disable_shared_mem && SMP_INIT && 
                MPI_SUCCESS == MPID_SMP_Check_incoming()) {
            progress = 1;
        }
#endif
        /* Poll RC FP channels */
        for(i = last_polled, j = 0; 
                j < mvdev.polling_set_size; 
                i = (i + 1) % mvdev.polling_set_size, ++j) {

            if(mvdev_check_rcfp(mvdev.polling_set[i])) {
                progress = 1;
                last_polled = i;
            }
        }

        /* Poll the CQ */
        wc.imm_data = 0;
        for(i = 0; i < mvdev.num_cqs; i++) {
            ne = ibv_poll_cq(mvdev.cq[i], 1, &wc);
            if(MVDEV_UNLIKELY(ne < 0)) {
                error_abort_all(IBV_RETURN_ERR, "Error polling CQ\n");
            } else if (1 == ne) {
                if (wc.status != IBV_WC_SUCCESS) {
                    MV_Poll_CQ_Error(&wc);
                }       

                switch(wc.opcode) {
                    case IBV_WC_RDMA_WRITE:
                    case IBV_WC_RDMA_READ:
                        {
                            mv_sdescriptor *d = (mv_sdescriptor *) wc.wr_id;
                            if(MVDEV_LIKELY(MVDEV_RC_FP == ((mv_sbuf *)(d->parent))->flag)) {
                                mvdev_process_send(d, d->parent);
                            } else {
                                mv_qp *qp = (mv_qp *) d->qp;
                                mv_sbuf *v = (mv_sbuf *)(d->parent);
                                ++(qp->send_wqes_avail);
                                if(NULL != qp->ext_sendq_head && qp->send_wqes_avail > 0) {
                                    mvdev_ext_sendq_send(qp);
                                } 
                                v->in_progress = 0;

                                if(0 == v->seqnum || v->rel_type == MV_RELIABLE_NONE) {
                                    release_mv_sbuf((mv_sbuf *)(d->parent));
                                }
                            }

                            break;
                        }
                    case IBV_WC_SEND:
                        {
                            mv_sdescriptor *d = (mv_sdescriptor *) wc.wr_id;
                            mvdev_process_send(d, d->parent);
                            break;
                        }
                    case IBV_WC_RECV:
                        {
                            mv_rdescriptor *d = (mv_rdescriptor *) wc.wr_id;;
                            mv_rbuf *v = d->parent;

                            if(MVDEV_LIKELY(0 == wc.imm_data)) {
                                mvdev_packet_mheader *mh = (mvdev_packet_mheader *) v->mheader_ptr;
                                v->has_header = mh->has_header;
                                v->rank       = mh->src_rank;
                                v->seqnum     = mh->seqnum;
                                SET_BYTE_LEN_HEADER(v, wc);
                                SET_RECV_HEADER(v);

                            } else {
                                READ_THS_TUPLE(wc.imm_data, &(v->has_header), 
                                        &(v->rank), &(v->seqnum));
                                SET_BYTE_LEN_IMM(v, wc);
                                SET_RECV_IMM(v);
                            }

                            v->data_ptr = v->header_ptr;

                            mvdev_process_recv(v);
                            ++recv;
                            break;
                        }
                    case IBV_WC_RECV_RDMA_WITH_IMM:
                        {
                            mv_rdescriptor *d = (mv_rdescriptor *) wc.wr_id;
                            mv_rbuf *v = d->parent;
                            DECR_RPOOL(((mv_rpool *) (v->rpool)));
                            CHECK_RPOOL(((mv_rpool *) (v->rpool)));
                            MV_Rndv_Put_Unreliable_Receive_Imm(v, wc.imm_data);
                            release_mv_rbuf(v);
                            break;
                        }
                    default:
                        error_abort_all(IBV_RETURN_ERR, "Invalid opcode\n");
                }

                progress = 1;
                empty = 0;
                wc.imm_data = 0;
            } else {
                empty = 1;
                ++count;

                if(count % 1200 == 0) {
                    if(get_time_us() - mvdev.last_check > mvparams.progress_timeout) {
                        mvdev.last_check = get_time_us();
                        mvdev_unackq_traverse();
                        SEND_ACKS();
                        recv = 0;
                    } else if(recv > 20) {
                        SEND_ACKS();
                        recv = 0;
                    }
                }
            }
        }

        if(flowlist) {
            process_flowlist();
        }

    } while ((blocking && !progress) || !empty);

    if(recv > mvparams.send_ackafter_progress || 
            (get_time_us() - mvdev.last_check > mvparams.progress_timeout)) {
        mvdev_unackq_traverse();
        SEND_ACKS();
        mvdev.last_check = get_time_us();
        recv = 0;
    }

    return progress ? MPI_SUCCESS : -1;
}


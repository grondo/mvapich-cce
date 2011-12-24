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

#include "mv_dev.h"
#include "mv_priv.h"
#include "mv_buf.h"
#include "mv_inline.h"
#include "dreg.h"
#include "ibverbs_header.h"
#include <math.h>
#include "req.h"


#define RELEASE_RNDV_QP(_qp) {                           \
    if(NULL != mvdev.rndv_pool_qps_free_head) {          \
        _qp->ptr.next = mvdev.rndv_pool_qps_free_head;   \
        mvdev.rndv_pool_qps_free_head = _qp;             \
    } else {                                             \
        _qp->ptr.next = NULL;                            \
        mvdev.rndv_pool_qps_free_head = _qp;             \
    }                                                    \
}

#define GET_RNDV_QP(_qp) {                                                      \
    _qp = mvdev.rndv_pool_qps_free_head;                                        \
    mvdev.rndv_pool_qps_free_head = mvdev.rndv_pool_qps_free_head->ptr.next;    \
    _qp->ptr.next = NULL;                                                       \
}


static inline void mvdev_post_zcopy_recv(MPIR_RHANDLE * rhandle) 
{
    mv_qp_pool_entry *rqp = (mv_qp_pool_entry *) rhandle->qp_entry;
    char * current_buf;
    int posted_buffers = ceil((double) rhandle->len / mvparams.mtu);;

    rhandle->start_seq = 0;

    if(posted_buffers <= 0) { 
        fprintf(stderr, "Posted buffers are zero or less! %d\n", posted_buffers);
    }

    {
        int current_len = 0, bytes_to_post = 0;
        int i = 0, j = 0;
        struct ibv_recv_wr *bad_wr;
        struct ibv_recv_wr rr[50];
        struct ibv_sge sge_entry[100];

        current_buf = rhandle->buf;

        while(current_len < rhandle->len) {
            for(i = 0; i < 50; i++) {
                MV_ASSERT(j < posted_buffers);

                bytes_to_post = MIN(mvparams.mtu, (rhandle->len - current_len));

                if(i > 0) {
                    rr[i - 1].next = &(rr[i]);
                }

                rr[i].next = NULL;
                rr[i].wr_id = j;
                rr[i].num_sge = 2;
                rr[i].sg_list = &(sge_entry[i * 2]);

                sge_entry[i * 2].addr   = (uintptr_t) mvdev.grh_buf;
                sge_entry[i * 2].length = 40;
                sge_entry[i * 2].lkey   = mvdev.grh_mr[0]->lkey;

                sge_entry[i * 2 + 1].addr   = (uintptr_t) (current_buf + current_len);
                sge_entry[i * 2 + 1].length = bytes_to_post;
                sge_entry[i * 2 + 1].lkey   =
                    ((dreg_entry *) (rhandle->dreg_entry))->memhandle[0]->lkey;

                current_len += bytes_to_post;

                j++;

                if(current_len >= rhandle->len) {
                    break;
                }
            }

            if(ibv_post_recv(rqp->ud_qp, rr, &bad_wr)) {
                error_abort_all(IBV_RETURN_ERR,"cannot post recv (%d)\n", i);
            }

        }

        MV_ASSERT(current_len == rhandle->len);
    }

}

void mvdev_flush_qp(mv_qp_pool_entry *rqp, int num_to_flush) 
{
    struct ibv_qp_attr qp_attr;
    struct ibv_wc wc;
    int ne;

    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.qp_state = IBV_QPS_ERR;

    /* need to transition to the error state so we can flush
     * all the posted buffers
     */
    if(ibv_modify_qp(rqp->ud_qp, &qp_attr, IBV_QP_STATE)) {
        error_abort_all(IBV_RETURN_ERR, "Error changing to the err state\n");
    }

    /* pull failed completions */
    {
        int total_pulled = 0;
        do {
            ne = ibv_poll_cq(rqp->ud_cq, 1, &wc);
            total_pulled += ne;
        } while(total_pulled < num_to_flush);
    }

    {
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_attr));

        attr.qp_state = IBV_QPS_RESET;

        if (ibv_modify_qp(rqp->ud_qp, &attr, IBV_QP_STATE)) {
            error_abort_all(IBV_RETURN_ERR,
                    "Failed to modify QP to RESET");
        }       
    }

    /* now we need to re-transition it back to the RTS phase */
    MV_Transition_UD_QP(&mvdev.rndv_si, rqp->ud_qp);
}

void mvdev_incoming_ud_zcopy_finish(mv_rbuf * v, mvdev_connection_t * c,
        mvdev_packet_ud_zcopy_finish * h)
{
    MPIR_RHANDLE *rhandle;
    mv_qp_pool_entry *rqp;
    struct ibv_wc *wc;
    int ne, count = 0, empty = 0, i = 0, next_to_recv = 0, in_order = 1;
    int posted_buffers;

    /* find the rhandle for this data */
    rhandle = (MPIR_RHANDLE *) ID_TO_REQ(h->rreq);
    rqp = (mv_qp_pool_entry *) rhandle->qp_entry;

    /* make sure all the data is here by checking the associated
     * cq for the qp used for this data transfer. All messages
     * are hopefully here.... otherwise we need to do
     * cleanup
     */

    D_PRINT("Got a zcopy finish message\n");

    posted_buffers = ceil((double) rhandle->len / mvparams.mtu);
    wc = (struct ibv_wc *) malloc(sizeof(struct ibv_wc) * posted_buffers);

    do {
        ne = ibv_poll_cq(rqp->ud_cq, posted_buffers - 1, wc);

        if(ne < 0) {
            error_abort_all(IBV_RETURN_ERR, "Error polling CQ\n");
        }
        else if (ne > 0) {

            for(i = 0; i < ne; i++) {
                if (wc[i].status != IBV_WC_SUCCESS) {
                    error_abort_all(IBV_STATUS_ERR, "got completion with "
                            "error. code: %d, wr_id: %lu", wc[i].status, wc[i].wr_id);
                } else {
                    if(IBV_WC_RECV == wc[i].opcode) {
                        if(wc[i].imm_data != next_to_recv) {
                            D_PRINT("Out of order! %u %u\n", wc[i].imm_data, next_to_recv);
                            in_order = 0;
                        }
                        next_to_recv++;
                    }
                }
                count++;
            }
        } else {
            empty = 1;
        }
    } while(!empty && posted_buffers != count);

    D_PRINT("Finished polling... -- got %d or %d\n", count, posted_buffers);

    if(count == posted_buffers && in_order) {
        mv_sbuf *v = get_sbuf(c, sizeof(mvdev_packet_ud_zcopy_ack));
        mvdev_packet_ud_zcopy_ack *h =
            (mvdev_packet_ud_zcopy_ack *) v->header_ptr;
        PACKET_SET_HEADER(h, c, MVDEV_PACKET_UD_ZCOPY_ACK);
        h->sreq = rhandle->send_id;
        v->shandle = NULL;

        mvdev_post_channel_send(c, v, sizeof(mvdev_packet_ud_zcopy_ack));

        D_PRINT("finished sending MVDEV_PACKET_UD_ZCOPY_ACK\n");

        RELEASE_RNDV_QP(rqp);

        RECV_COMPLETE(rhandle);

    } else {
        if(count != posted_buffers) {
            mvdev_flush_qp((mv_qp_pool_entry *) rhandle->qp_entry,
                    posted_buffers - count);
        }
        mvdev_post_zcopy_recv(rhandle);
        MV_Rndv_Send_Reply(rhandle);
    }

    free(wc);
}




void mvdev_incoming_ud_zcopy_ack(mv_rbuf * v, mvdev_connection_t * c,
        mvdev_packet_ud_zcopy_ack * h)
{
    MPIR_SHANDLE *shandle = (MPIR_SHANDLE *) ID_TO_REQ(h->sreq);
    D_PRINT("got zcopy ack\n");

    SEND_COMPLETE(shandle);
}

void mvdev_recv_ud_zcopy(MPIR_RHANDLE * rhandle)
{
    mv_qp_pool_entry *rqp;

    D_PRINT("got zcopy start -- len: %d\n", rhandle->len);

    /* only way buffer is NULL is if length is zero */
    if (NULL == rhandle->buf) {
        rhandle->buf = &nullrbuffer;
    }

    /* we need to make sure we have a QP available -- 
     * otherwise we don't want to take this path 
     */
    if(NULL == mvdev.rndv_pool_qps_free_head) {
        D_PRINT("No QPs available -- using R3\n");

        rhandle->protocol = MVDEV_PROTOCOL_R3;
        mvdev_recv_r3(rhandle);
        return; 
    }

    /* try to register the buffer directly */
    rhandle->dreg_entry = dreg_register(rhandle->buf,
            rhandle->len, DREG_ACL_WRITE);
    if (NULL == rhandle->dreg_entry) {
        /* failed to register memory, revert to R3 */
        D_PRINT("Cannot register mem -- using R3\n");
        rhandle->protocol = MVDEV_PROTOCOL_R3;
        mvdev_recv_r3(rhandle);
        return; 
    }

    GET_RNDV_QP(rqp);
    rhandle->qp_entry = rqp;

    MV_ASSERT(rhandle->qp_entry != NULL);
    D_PRINT("before posting recv\n");

    mvdev_post_zcopy_recv(rhandle);

    D_PRINT("Finished posting buffers\n");
    MV_Rndv_Send_Reply(rhandle);
}


inline void mvdev_ud_zcopy_finish(MPIR_SHANDLE * s, int ah_index)
{
    mvdev_connection_t *c = (mvdev_connection_t *) (s->connection);
    mv_sbuf *v = get_sbuf_ud(c, sizeof(mvdev_packet_ud_zcopy_finish));
    mvdev_packet_ud_zcopy_finish *packet = 
        (mvdev_packet_ud_zcopy_finish *) v->header_ptr;

    D_PRINT("sending a zcopy finish message for %p\n", s);

    /* fill in the packet */
    PACKET_SET_HEADER(packet, c, MVDEV_PACKET_UD_ZCOPY_FINISH);
    packet->rreq = REQ_TO_ID(s->receive_id);

    /* mark MPI send complete when we get an ACK for this message */
    v->shandle = s;

    /* needs to be on the same outgoing qp as the data */
    mvdev_post_channel_send_qp_lid(c, v, sizeof(mvdev_packet_ud_zcopy_finish), 
            &(mvdev.rndv_qp[s->hca_index]), ah_index);

    /* in case the finish message gets dropped we need to send again --
     * even if we haven't pulled it from the CQ 
     */
    v->retry_always = 1;

    D_PRINT("finished sending finish message\n");
}

void mvdev_rendezvous_push_zcopy(MPIR_SHANDLE * s, mvdev_connection_t *c) {
    int pkt_count, malloc_count, bytes_to_send, i, ne;
    struct ibv_send_wr *sr, *bad_wr;
    struct ibv_sge *sg_entry;
    struct ibv_wc wc_list[30];
    mv_qp *qp = &(mvdev.rndv_qp[s->hca_index]);

    MV_ASSERT(s->dreg_entry != NULL);

    pkt_count = ceil((double) s->bytes_total / mvparams.mtu);
    malloc_count = MIN(pkt_count, 64);

    D_PRINT("Sending %u of data for shandle %p\n", s->bytes_total,  REQ_TO_ID(s));
    D_PRINT("Local addr: %p\n", s->local_address);

    sr = (struct ibv_send_wr *) malloc(sizeof(struct ibv_send_wr) * malloc_count);
    sg_entry = (struct ibv_sge *) malloc(sizeof(struct ibv_sge) * malloc_count);
    
    D_PRINT("Entering push zcopy (%d)\n", s->bytes_total);

    c->last_ah = (c->last_ah + 1) % mvparams.max_lmc_total;

    while(s->bytes_sent < s->bytes_total) {
        int empty = 0;

        do {
            ne = ibv_poll_cq(mvdev.rndv_cq[s->hca_index], 30, wc_list);
            if(ne < 0) {
                error_abort_all(IBV_RETURN_ERR, "Error polling RNDV CQ\n");
            } else if (ne > 0) {
                for(i = 0; i < ne; i++) {
                    if(wc_list[i].status != IBV_WC_SUCCESS) {
                        error_abort_all(IBV_STATUS_ERR, "got completion with "
                                "error code %d, wr_id: %lu\n",
                                wc_list[i].status, wc_list[i].wr_id);
                    }
                    qp->send_wqes_avail++;

                    if(wc_list[i].wr_id) {
                        mv_sbuf * v = (mv_sbuf *) ((mv_sdescriptor *) wc_list[i].wr_id)->parent;
                        v->left_to_send--;
                        if(0 == v->left_to_send) {
                            v->in_progress = 0;
                            if(0 == v->seqnum) {
                                release_mv_sbuf(v);
                            }
                        }
                    }
                }
                empty = 0;
            } else {
                empty = 1;
            }

        } while(qp->send_wqes_avail < 500 || !empty);


        for(i = 0; i < malloc_count; i++) {
            bytes_to_send = MIN(s->bytes_total - s->bytes_sent, mvparams.mtu);

            if(i > 0) {
                sr[i-1].next = &(sr[i]);
            } 
            
            sr[i].next     = NULL;
            sr[i].opcode   = IBV_WR_SEND_WITH_IMM;
            sr[i].wr_id    = 0;
            sr[i].num_sge  = 1;
            sr[i].sg_list  = &(sg_entry[i]);
            sr[i].imm_data = s->seqnum++;
            sr[i].send_flags = IBV_SEND_SIGNALED;

            sr[i].wr.ud.ah = c->data_ud_ah[c->last_ah];

            sr[i].wr.ud.remote_qpn  = s->remote_qpn;
            sr[i].wr.ud.remote_qkey = 0;

            sg_entry[i].addr   = (uintptr_t) ((char *) (s->local_address) + s->bytes_sent);
            sg_entry[i].length = bytes_to_send;
            sg_entry[i].lkey   = ((dreg_entry *) s->dreg_entry)->memhandle[0]->lkey;

            s->bytes_sent += bytes_to_send;

            qp->send_wqes_avail--;

            if(s->bytes_total == s->bytes_sent) {
                break;
            }
        }

        if(ibv_post_send(qp->qp, sr, &bad_wr)) {
            error_abort_all(IBV_RETURN_ERR,"Error posting to UD RNDV QP (%d) - %lu\n", 
                    qp->send_wqes_avail, bad_wr->wr_id );
        }

    }

    MV_ASSERT(s->bytes_total == s->bytes_sent);

    mvdev_ud_zcopy_finish(s, c->last_ah);
    s->nearly_complete = 1;
}




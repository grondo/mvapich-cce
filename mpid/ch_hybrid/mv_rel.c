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

#include "mv_priv.h"
#include "mv_inline.h"
#include "mv_buf.h"
#include "mv_packet.h"

void mvdev_resend(mv_sbuf *v) {
    int i;
    mv_qp *qp;

    D_PRINT("retry\n");

    v->retry_count++;
    v->in_progress = 1;

    if(v->retry_count > mvparams.max_retry_count) {
        error_abort_all(IBV_RETURN_ERR,"Exceeded maximum retry to %d\n", v->rank);
    }

    if(NULL == v->desc_chunk) {
        MV_ASSERT(v->segments == 1);
        qp = (mv_qp *) v->desc.qp;
        mvdev_packet_header * p = (mvdev_packet_header *) v->header_ptr;
        mvdev_connection_t *c = &(mvdev.connections[v->rank]);
        p->last_recv = c->seqnum_next_toack;
        mvdev.connection_ack_required[v->rank] = 0;

        D_PRINT("resending transport: %d, type: %d\n", v->transport, p->type);

        if(qp->send_wqes_avail <= 0) {
            mvdev_ext_sendq_queue(qp, &(v->desc));
        } else {
            struct ibv_send_wr *bad_wr;
            qp->send_wqes_avail--;
            if(ibv_post_send(qp->qp, &(v->desc.sr), &bad_wr)) {
                error_abort_all(IBV_RETURN_ERR,"Error posting to UD QP %d\n",
                        qp->send_wqes_avail);
            }
        }
    } else {
        int queue_all = 0;
        for(i = 0; i < v->segments; i++) {
            int wqes = 1;
            qp = (mv_qp *) v->desc.qp;

            v->left_to_send++;

            /* The first packet in a sbuf must have a real header */
            if(i == 0) {
                mvdev_packet_header * p = (mvdev_packet_header *) v->header_ptr;
                mvdev_connection_t *c = &(mvdev.connections[v->rank]);
                p->last_recv = c->seqnum_next_toack;
                mvdev.connection_ack_required[v->rank] = 0;
            }

            /* figure out if we chained the descriptors */
            {
                int iter = i;
                struct ibv_send_wr * temp_iter = &(v->desc_chunk->desc[iter].sr);
                while(NULL != temp_iter->next) {
                    temp_iter = temp_iter->next;
                    iter++;
                    wqes++;
                }
            }

            if(queue_all || qp->send_wqes_avail < wqes) {
                mvdev_ext_sendq_queue(qp, &(v->desc_chunk->desc[i]));
                queue_all = 1;
            } else {
                struct ibv_send_wr *bad_wr;

                qp->send_wqes_avail -= wqes;

                if(ibv_post_send(qp->qp, &(v->desc_chunk->desc[i].sr), &bad_wr)) {
                    error_abort_all(IBV_RETURN_ERR,"Error posting to UD QP %d\n",
                            qp->send_wqes_avail);
                }
            }
        }
    }
}



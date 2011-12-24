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

struct ibv_qp * MV_Create_RC_QP(mv_qp_setup_information *si) {

    struct ibv_qp * qp = NULL;

    /* create */
    {
        struct ibv_qp_init_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_init_attr));

        attr.srq     = si->srq;
        D_PRINT("SRQ at create qp: %p\n", attr.srq);

        attr.send_cq = si->send_cq;
        attr.recv_cq = si->recv_cq;

        attr.cap.max_send_wr = si->cap.max_send_wr;
        attr.cap.max_recv_wr = si->cap.max_recv_wr;
        attr.cap.max_send_sge = si->cap.max_send_sge;
        attr.cap.max_recv_sge = si->cap.max_recv_sge;
        attr.cap.max_inline_data = si->cap.max_inline_data;
        attr.qp_type = IBV_QPT_RC;

        qp = ibv_create_qp(si->pd, &attr);
        if (!qp) {
            error_abort_all(IBV_RETURN_ERR, "Couldn't create RC QP");
            return NULL;
        }
    }

    /* init */
    {
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_attr));
        attr.qp_state = IBV_QPS_INIT;
        attr.pkey_index = mvparams.pkey_ix;
        attr.port_num = mvparams.default_port;
        attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE |
            IBV_ACCESS_REMOTE_READ;

        attr.pkey_index      = 0;
       

        if(ibv_modify_qp(qp, &attr, 
                    IBV_QP_STATE |
                    IBV_QP_PKEY_INDEX |
                    IBV_QP_PORT |
                    IBV_QP_ACCESS_FLAGS)) {
            error_abort_all(IBV_RETURN_ERR, "Failed to modify RC QP to INIT");
            return NULL;
        }
    }

    mvdev.rc_connections++;

    return qp;
}


mvdev_channel_rc * MV_Setup_RC(mvdev_connection_t *c, int buf_size, int type, int rq_size) {
    int hca = 0;
    mv_qp_setup_information si;

    /* TODO: use buf_size */

    mvdev_channel_rc *ch = (mvdev_channel_rc *) malloc(sizeof(mvdev_channel_rc));

    ch->buffer_size = buf_size;
    ch->type = type;
    ch->next = NULL;

    mv_qp * qp = &(ch->qp[0]);

    si.send_cq = si.recv_cq = mvdev.cq[hca];

    if(MVDEV_CH_RC_SRQ == type) {
        mv_srq * srq = MV_Get_SRQ(buf_size);
        si.srq = srq->srq;
        si.cap.max_recv_wr = 0;
    } else {
        si.cap.max_recv_wr = rq_size + 15;
        si.srq = NULL;
    }

    si.pd = mvdev.hca[hca].pd;
    si.sq_psn = mvparams.psn;
    si.cap.max_send_sge = 1;
    si.cap.max_recv_sge = 1;
    si.cap.max_send_wr = mvparams.rc_sq_size;

    if(-1 != mvparams.rc_max_inline) {
        si.cap.max_inline_data = mvparams.rc_max_inline;
    } else {
        si.cap.max_inline_data = 0;
    }

    qp->qp = MV_Create_RC_QP(&si);
    qp->send_wqes_avail = mvparams.rc_sq_size - 5;
    qp->send_wqes_total = mvparams.rc_sq_size - 5;
    qp->ext_sendq_head = qp->ext_sendq_tail = NULL;
    qp->ext_backlogq_head = qp->ext_backlogq_tail = NULL;
    qp->hca = &(mvdev.hca[hca]);
    qp->ext_sendq_size = 0;
    qp->unsignaled_count = 0;
    qp->max_send_size = buf_size;
    qp->type = type;

    {
        struct ibv_qp_attr attr;
        struct ibv_qp_init_attr init_attr;

        ibv_query_qp(qp->qp, &attr, 0, &init_attr);
        qp->max_inline = init_attr.cap.max_inline_data;
    }

    if(MVDEV_CH_RC_RQ == type) {
        qp->send_credits_remaining = rq_size;
    }
     
    return ch;
}

mvdev_channel_rc * MV_Setup_RC_SRQ(mvdev_connection_t *c, int buf_size) {
    return MV_Setup_RC(c, buf_size, MVDEV_CH_RC_SRQ, 0);
}

mvdev_channel_rc * MV_Setup_RC_RQ(mvdev_connection_t *c, int buf_size, int credit) {
    mvdev_channel_rc * ch = MV_Setup_RC(c, buf_size, MVDEV_CH_RC_RQ, credit);

    /* need to have an rpool for the receive buffers */
    ch->qp[0].rpool = MV_Create_RPool(credit + 15, 1, buf_size, NULL, ch->qp);

    /* we need to now fill the rpool */
    CHECK_RPOOL(ch->qp[0].rpool);

    return ch;
}

void MV_Transition_RC_QP(struct ibv_qp * qp, uint32_t dest_qpn, 
        uint16_t dest_lid, uint8_t dest_src_path_bits) {
    {
        struct ibv_qp_attr attr;

        dest_src_path_bits = 0;

        memset(&attr, 0, sizeof(struct ibv_qp_attr));
        attr.qp_state = IBV_QPS_RTR;
        attr.path_mtu = mvparams.rc_mtu;

        attr.max_dest_rd_atomic = mvparams.qp_ous_rd_atom;
        attr.min_rnr_timer = mvparams.min_rnr_timer;
        attr.ah_attr.sl = mvparams.service_level;
        attr.ah_attr.port_num = mvparams.default_port;
        attr.ah_attr.static_rate = mvparams.static_rate;

        attr.ah_attr.dlid = dest_lid;
        attr.dest_qp_num = dest_qpn;
        attr.ah_attr.src_path_bits = dest_src_path_bits;

        attr.rq_psn = 0;
        attr.max_dest_rd_atomic = 1;
        attr.min_rnr_timer      = 12;
        attr.ah_attr.is_global  = 0;
        attr.ah_attr.sl         = 0;
        attr.ah_attr.src_path_bits = 0;
        attr.path_mtu           = IBV_MTU_2048;


        /*
        if(ibv_modify_qp(qp, &attr, 
                    IBV_QP_STATE |
                    IBV_QP_AV |
                    IBV_QP_PATH_MTU |
                    IBV_QP_DEST_QPN |
                    IBV_QP_RQ_PSN |
                    IBV_QP_MAX_DEST_RD_ATOMIC | 
                    IBV_QP_MIN_RNR_TIMER
                    )) {
                    */
        if(ibv_modify_qp(qp, &attr, 
                    IBV_QP_STATE |
                    IBV_QP_AV |
                    IBV_QP_PATH_MTU |
                    IBV_QP_DEST_QPN |
                    IBV_QP_RQ_PSN |
                    IBV_QP_MAX_DEST_RD_ATOMIC | 
                    IBV_QP_MIN_RNR_TIMER
                    )) {
            error_abort_all(IBV_RETURN_ERR, "Failed to modify RC QP to RTR");
        }
    }

    /* rts */
    {
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_attr));
        attr.qp_state = IBV_QPS_RTS;
        attr.timeout = mvparams.time_out;
        attr.retry_cnt = mvparams.retry_count;
        attr.rnr_retry = mvparams.rnr_retry;
        attr.sq_psn = mvparams.psn;
        attr.max_rd_atomic = mvparams.qp_ous_rd_atom;

        if(ibv_modify_qp(qp, &attr, 
                    IBV_QP_STATE |
                    IBV_QP_TIMEOUT |
                    IBV_QP_RETRY_CNT |
                    IBV_QP_RNR_RETRY | 
                    IBV_QP_SQ_PSN | 
                    IBV_QP_MAX_QP_RD_ATOMIC
                    )) {
            error_abort_all(IBV_RETURN_ERR, "Failed to modify RC QP to RTR");
        }
    }
}


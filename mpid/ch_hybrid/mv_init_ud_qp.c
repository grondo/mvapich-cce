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

void MV_Transition_UD_QP(mv_qp_setup_information *si, struct ibv_qp * qp) {

    {
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_attr));

        attr.qp_state = IBV_QPS_INIT;
        attr.pkey_index = 0;
        attr.port_num = 1;
        attr.qkey = 0;

        if (ibv_modify_qp(qp, &attr,
                    IBV_QP_STATE |
                    IBV_QP_PKEY_INDEX |
                    IBV_QP_PORT | IBV_QP_QKEY)) {
            error_abort_all(IBV_RETURN_ERR,
                    "Failed to modify QP to INIT");
        }       
    }

    {
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_attr));

        attr.qp_state = IBV_QPS_RTR;
        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE)) {
            error_abort_all(IBV_RETURN_ERR, "Failed to modify QP to RTR");
        }
    }

    {
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_attr));

        attr.qp_state = IBV_QPS_RTS;
        attr.sq_psn = si->sq_psn;
        if (ibv_modify_qp(qp, &attr, 
                    IBV_QP_STATE | IBV_QP_SQ_PSN)) {
            error_abort_all(IBV_RETURN_ERR, "Failed to modify QP to RTS");
        }
    }


}

struct ibv_qp * MV_Setup_UD_QP(mv_qp_setup_information *si) {

    struct ibv_qp * qp;

    {
        struct ibv_qp_init_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_init_attr));
        attr.send_cq = si->send_cq;
        attr.recv_cq = si->recv_cq;

        attr.cap.max_send_wr = si->cap.max_send_wr;
        attr.cap.max_recv_wr = si->cap.max_recv_wr;
        attr.cap.max_send_sge = si->cap.max_send_sge;
        attr.cap.max_recv_sge = si->cap.max_recv_sge;
        attr.cap.max_inline_data = si->cap.max_inline_data;
        attr.qp_type = IBV_QPT_UD;

        qp = ibv_create_qp(si->pd, &attr);
        if (!qp) {
            error_abort_all(IBV_RETURN_ERR, "Couldn't create UD QP");
            return NULL;
        }

    }

    
    MV_Transition_UD_QP(si, qp);

    return qp;
}

/* scatter the QPs across the different HCAs -- to 
 * stripe across different HCAs we may need to keep separate
 * pools
 */

int MV_Setup_Rndv_QPs() {
    int i;

    mvdev.rndv_pool_qps = (mv_qp_pool_entry *) 
        malloc(sizeof(mv_qp_pool_entry) * mvparams.rndv_qps);
    mvdev.grh_buf = (char *) malloc(sizeof(char) * 40);

    mvdev.rndv_cq = (struct ibv_cq **) malloc(sizeof(struct ibv_cq *) * mvdev.num_hcas);
    mvdev.rndv_qp = (mv_qp *) malloc(sizeof(mv_qp) * mvdev.num_hcas);

    /* setup the pool of QPs */
    for(i = 0; i < mvparams.rndv_qps; i++) {
        int hca = i % mvdev.num_hcas;

        mvdev.rndv_pool_qps[i].ud_cq =
            ibv_create_cq(mvdev.hca[hca].context, 8192 * 2, NULL, NULL, 0);
        if(!mvdev.rndv_pool_qps[i].ud_cq) {
            error_abort_all(IBV_RETURN_ERR, "Couldn't create RNDV CQ %d", i);
            return 0;
        }

        mvdev.rndv_si.recv_cq = mvdev.rndv_si.send_cq = mvdev.rndv_pool_qps[i].ud_cq;
        mvdev.rndv_si.sq_psn = mvparams.psn;
        mvdev.rndv_si.pd = mvdev.hca[hca].pd;

        mvdev.rndv_si.cap.max_send_wr = 1;
        mvdev.rndv_si.cap.max_recv_wr = 4096;
        mvdev.rndv_si.cap.max_send_sge = 1;
        mvdev.rndv_si.cap.max_recv_sge = 2;
        mvdev.rndv_si.cap.max_inline_data = 0;

        mvdev.rndv_pool_qps[i].ud_qp = MV_Setup_UD_QP(&mvdev.rndv_si);
        if(!mvdev.rndv_pool_qps[i].ud_qp) {
            error_abort_all(IBV_RETURN_ERR, "Couldn't create RNDV UD QP %d", i);
        }

        mvdev.rndv_pool_qps[i].associated_qpn  = -1;
        mvdev.rndv_pool_qps[i].associated_rank = -1;
        mvdev.rndv_pool_qps[i].seqnum          = 0;

        mvdev.rndv_pool_qps[i].ptr.next = NULL;
        mvdev.rndv_pool_qps[i].ptr.prev = NULL;

        mvdev.rndv_pool_qps[i].hca = &(mvdev.hca[hca]);
    }

    for(i = 0; i < mvparams.rndv_qps - 1; i++) {
        mvdev.rndv_pool_qps[i].ptr.next =
            &(mvdev.rndv_pool_qps[i + 1]);
    }
    mvdev.rndv_pool_qps_free_head = &(mvdev.rndv_pool_qps[0]);

    /* setup the cqs for completions of send ops */
    for(i = 0; i < mvdev.num_hcas; i++) {
        mvdev.rndv_cq[i] =
            ibv_create_cq(mvdev.hca[i].context, 16384, NULL,
                    NULL, 0);
        if (!mvdev.rndv_cq[i]) {
            error_abort_all(IBV_RETURN_ERR, "Couldn't create RNDV CQ");
            return 0;
        }

        /* register the GRH buffer for each HCA */
        mvdev.grh_mr[i]  = register_memory(i, mvdev.grh_buf, 40);
    }

    /* setup the qps we send zcopy messages on */
    for(i = 0; i < mvdev.num_hcas; i++) {
        mv_qp_setup_information si;

        si.send_cq = si.recv_cq = mvdev.rndv_cq[i];
        si.sq_psn = mvparams.psn;
        si.pd = mvdev.hca[i].pd;
        si.cap.max_send_wr = 15000;
        si.cap.max_recv_wr = 1;
        si.cap.max_send_sge = 1;
        si.cap.max_recv_sge = 1;
        si.cap.max_inline_data = 0;

        mvdev.rndv_qp[i].qp = MV_Setup_UD_QP(&si);
        mvdev.rndv_qp[i].send_wqes_avail = 15000;
        mvdev.rndv_qp[i].send_wqes_total = 15000;
        mvdev.rndv_qp[i].ext_sendq_head = mvdev.rndv_qp[i].ext_sendq_tail = NULL;
        mvdev.rndv_qp[i].hca = &(mvdev.hca[i]);
        mvdev.rndv_qp[i].type = MVDEV_CH_UD_RQ;
    }

    return 0;
}


int MV_Setup_QPs() {
    int i = 0;
    int port = 0;

    D_PRINT("Num HCAs: %d\n", mvdev.num_hcas);

    mvdev.cq = (struct ibv_cq **) malloc(sizeof(struct ibv_cq *) * mvdev.num_hcas);
    mvdev.ud_qp = (mv_qp *) malloc(sizeof(mv_qp) * mvdev.num_hcas * mvparams.num_qps);

    mvdev.num_cqs = mvdev.num_hcas;
    mvdev.num_ud_qps = mvdev.num_hcas * mvparams.num_qps;

    /* create one data cq for each HCA */
    for(i = 0; i < mvdev.num_hcas; i++) {
        mvdev.cq[i] =
            ibv_create_cq(mvdev.hca[i].context, mvparams.cq_size, NULL,
                    NULL, 0);
        if (!mvdev.cq[i]) {
            error_abort_all(IBV_RETURN_ERR, "Couldn't create Data CQ");
            return 0;
        }
    }

    for(port = 0; port < mvparams.num_qps; port++) {
    for(i = 0; i < mvdev.num_hcas; i++) {
        int index = (port * mvdev.num_hcas) + i;
        D_PRINT("index is %d\n", index);

        /* Setup the UD QP for normal data transfer */
        mv_qp_setup_information si;

        si.send_cq = si.recv_cq = mvdev.cq[i];
        si.sq_psn = mvparams.psn;
        si.pd = mvdev.hca[i].pd;
        si.cap.max_send_wr = mvparams.ud_sq_size;
        si.cap.max_recv_wr = mvparams.ud_rq_size;
        si.cap.max_send_sge = 1;
        si.cap.max_recv_sge = 1;

        if(mvparams.ud_max_inline != -1) {
            si.cap.max_inline_data = mvparams.ud_max_inline;
        } else {
            si.cap.max_inline_data = 0;
        }

        mvdev.ud_qp[index].qp = MV_Setup_UD_QP(&si);

        if(!mvdev.ud_qp[index].qp) {
            error_abort_all(IBV_RETURN_ERR, "Couldn't create data QP");
        }

        mvdev.ud_qp[index].send_wqes_avail = mvparams.ud_sq_size - 50;
        mvdev.ud_qp[index].send_wqes_total = mvparams.ud_sq_size - 50;
        mvdev.ud_qp[index].ext_sendq_head = mvdev.ud_qp[index].ext_sendq_tail = NULL;
        mvdev.ud_qp[index].hca = &(mvdev.hca[i]);
        mvdev.ud_qp[index].ext_sendq_size = 0;
        mvdev.ud_qp[index].unsignaled_count = 0;
        mvdev.ud_qp[index].type = MVDEV_CH_UD_RQ;


        {
            struct ibv_qp_attr attr;
            struct ibv_qp_init_attr init_attr;

            ibv_query_qp(mvdev.ud_qp[index].qp, &attr, 0, &init_attr);
            mvdev.ud_qp[index].max_inline = init_attr.cap.max_inline_data;
        }


        /* get a receive pool setup for this qp */
        mvdev.ud_qp[index].rpool = MV_Create_RPool(mvparams.recvq_size, 100, mvparams.mtu, NULL, &(mvdev.ud_qp[index]));

        D_PRINT("Finished setting up UD QP %d, num: %u\n", i, mvdev.ud_qp[i].qp->qp_num);
    }
    }


    return 1;
}

void MV_Generate_Address_Handles() {
    int i;
    /*Create address handles */
    for (i = 0; i < mvdev.np; i++) {
        mvdev_connection_t * c = &(mvdev.connections[i]);
        int j;
        int total_count = 0;

        int k;
        for(k = 0; k < mvparams.max_sl; k++) {
        for(j = 0; j < mvparams.max_lmc_total; j++) {
                struct ibv_ah_attr ah_attr;
                memset(&ah_attr, 0, sizeof(ah_attr));
                ah_attr.is_global = 0;
                ah_attr.dlid = mvdev.lids[i] + j;
                ah_attr.sl = k;
                ah_attr.src_path_bits = 0;
                ah_attr.port_num = mvparams.default_port;

                c->data_ud_ah[total_count] = ibv_create_ah(mvdev.default_hca->pd, &ah_attr);

                if (!c->data_ud_ah[total_count]) {
                    error_abort_all(IBV_RETURN_ERR, "Failed to create AH");
                }
                total_count++;
            }
        }

    }
    mvparams.max_ah_total = mvparams.max_lmc_total * mvparams.max_sl;
}


int MV_UD_Initialize_Connection(mvdev_connection_t * c) {
    return 0;
}






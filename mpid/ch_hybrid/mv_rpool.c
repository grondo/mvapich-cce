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


mv_rpool * MV_Create_RPool(int capacity, int loss, int buffer_size, mv_srq *srq, mv_qp *qp) 
{
    mv_rpool * rp = (mv_rpool *) malloc(sizeof(mv_rpool));

    rp->posted = 0;
    rp->capacity = capacity;
    rp->buffer_size = buffer_size;
    rp->srq = srq;
    rp->qp = qp; 
    rp->next = NULL;

    rp->credit_preserve = capacity - loss;

    return rp; 
}

mv_srq * MV_Create_SRQ(int buffer_size, int recvq_size, int limit)
{
    int hca = 0;
    struct ibv_srq_init_attr srq_init_attr;
    mv_srq * srq = (mv_srq *) malloc(sizeof(mv_srq));

    srq->limit = limit;
    srq->buffer_size = buffer_size;
    srq->recvq_size = recvq_size;

    memset(&srq_init_attr, 0, sizeof(srq_init_attr));

    srq_init_attr.srq_context = mvdev.hca[hca].context;
    srq_init_attr.attr.max_wr = srq->recvq_size;
    srq_init_attr.attr.max_sge = 1;

    /* The limit value should be ignored during
     * SRQ create */
    srq_init_attr.attr.srq_limit = srq->limit;
    srq->srq = ibv_create_srq(mvdev.hca[hca].pd, &srq_init_attr);
    if (!srq->srq) {
        error_abort_all(IBV_RETURN_ERR, "Error creating SRQ\n");
    }   

    return srq;
}

mv_srq * MV_Get_SRQ(int buf_size) 
{
    mv_rpool * prev = NULL;
    mv_rpool * cur = mvdev.rpool_srq_list;

    while(NULL != cur && buf_size < cur->buffer_size) {
        prev = cur;
        cur = cur->next;
    } 

    if(NULL != cur && buf_size == cur->buffer_size) {
        return cur->srq;
    } else {
        mv_srq * new_srq = MV_Create_SRQ(buf_size, mvparams.srq_size, 10);
        mv_rpool * new_rpool = MV_Create_RPool(mvparams.srq_size, 100, buf_size, new_srq, NULL);
        CHECK_RPOOL(new_rpool);

        if(NULL == prev) {
            new_rpool->next = NULL;
            mvdev.rpool_srq_list = new_rpool; 
        } else {
            new_rpool->next = prev->next;
            prev->next = new_rpool;
        }

        return new_srq;
    }

}





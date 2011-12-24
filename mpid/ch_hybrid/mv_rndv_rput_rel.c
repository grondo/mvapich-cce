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

void MV_Rndv_Put_Reliable_Init(mv_sbuf * v, void *local_address,
                    uint32_t lkey, void *remote_address,
                    uint32_t rkey, int len, mv_qp * qp, uint32_t srqn)
{

#ifdef XRC
    if(MVDEV_TRANSPORT_XRC == v->transport) {
        v->desc.sr.xrc_remote_srq_num = srqn;
    }
#endif

    v->desc.sr.next = NULL;
    v->desc.sr.send_flags = IBV_SEND_SIGNALED;
    v->desc.sr.opcode = IBV_WR_RDMA_WRITE;
    v->desc.sr.wr_id = (aint_t) (&(v->desc));

    v->desc.sr.num_sge = 1;
    v->desc.sr.sg_list = &(v->desc.sg_entry);

    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = lkey;
    v->desc.sg_entry.addr = (uintptr_t) local_address;

    v->desc.sr.wr.rdma.remote_addr = (uintptr_t) remote_address;
    v->desc.sr.wr.rdma.rkey = rkey;

    v->desc.qp = qp;
}



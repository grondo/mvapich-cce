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

#ifndef MPID_CM_USER_H
#define MPID_CM_USER_H

#include <infiniband/verbs.h>
#include "ibverbs_header.h"
#include "nfr.h"


void odu_enable_qp(int peer, struct ibv_qp * qp);
void odu_re_enable_qp(int peer);
    
void cm_process_queue(int peer_rank);

static inline void odu_test_new_connection()
{
    int i;  
    if (viadev.cm_new_connection == 0)
        return; 
    /* Note: to avoid race condition, must first put down flag, then process
     * pending sends*/
    viadev.cm_new_connection = 0; 

    V_PRINT(DEBUG03, "NR: New connection found\n");
    
    for (i=0; i<viadev.np; i++)
    {
        if (i==viadev.me)
            continue;

        if (viadev_use_nfr && QP_REC == viadev.connections[i].qp_status) {
            V_PRINT(DEBUG03, "[%d]connection request bad-%d %d %d %d\n", i, 
                    nfr_num_of_bad_connections, viadev.pending_req_head[i], 
                    cm_conn_state[i], viadev.connections[i].qp_status);
            nfr_num_of_bad_connections--; /* decreasing number of error connections */
            viadev.connections[i].qp_status = QP_REC_D;
        } else {
            V_PRINT(DEBUG03,"[%d]connection request %d %d %d\n", i, 
                    viadev.pending_req_head[i], cm_conn_state[i], 
                    viadev.connections[i].qp_status);
            if (viadev.pending_req_head[i] != NULL 
                    && MPICM_IB_RC_PT2PT == cm_conn_state[i] &&
                    QP_UP == viadev.connections[i].qp_status) {
                V_PRINT(DEBUG03,"First connection request \n !!!!");
                cm_process_queue(i);    
            }    
        }

        /*
        if (viadev.pending_req_head[i] != NULL 
          && MPICM_IB_RC_PT2PT == cm_conn_state[i]) {
          cm_process_queue(i);    
        }       
        */
    }
}


#endif

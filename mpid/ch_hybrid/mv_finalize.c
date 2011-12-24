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

#include "mv_dev.h"
#include "mv_param.h"
#include "mv_inline.h"
#include "mv_priv.h"
#include "mem_hooks.h"
#include "dreg.h"

void MPID_MV_Finalize(void) 
{
    int i;

#ifndef DISABLE_PTMALLOC
    mvapich_mfin();
#endif

    deallocate_mv_bufs();
    pmgr_finalize();
    while (dreg_evict());

    if(mvparams.use_zcopy) {
        /* destroy resources used for sending */
        for(i = 0; i < mvdev.num_hcas; i++) {
            ibv_destroy_qp(mvdev.rndv_qp[i].qp);
            ibv_destroy_cq(mvdev.rndv_cq[i]);
        }

        /* destroy resources used for recv */
        for(i = 0; i < mvparams.rndv_qps; i++) {
            ibv_destroy_qp(mvdev.rndv_pool_qps[i].ud_qp);
            ibv_destroy_cq(mvdev.rndv_pool_qps[i].ud_cq);
        }

        free(mvdev.rndv_pool_qps);
        free(mvdev.grh_buf);
        free(mvdev.rndv_cq);
        free(mvdev.rndv_qp);
    }

    for(i = 0; i < mvdev.np; i++) {
        int j;
        for(j = 0; j < mvparams.max_ah_total; j++) {
            ibv_destroy_ah(mvdev.connections[i].data_ud_ah[j]);
        }
        free(mvdev.connections[i].data_ud_ah);
    }

    for(i = 0; i < mvdev.num_ud_qps; i++) {
        ibv_destroy_qp(mvdev.ud_qp[i].qp);
    }
    free(mvdev.ud_qp);

    /* free all RC QPs that were created */
    for(i = 0; i < mvdev.np; i++) {
        mvdev_channel_rc * cur = mvdev.connections[i].rc_channel_head;
        mvdev_channel_rc * next;

        if(NULL != cur) {
            ibv_destroy_qp(cur->qp[0].qp);
            next = cur->next; 
            free(cur);
            cur = next;
        }
    }

    /* free all SRQs used for RC */
    {
        mv_rpool * cur = mvdev.rpool_srq_list;
        mv_rpool * next = NULL;
        if(NULL != cur) {
            ibv_destroy_srq(cur->srq->srq);
            free(cur->srq);
            next = cur->next;
            free(cur);
            cur = next; 
        }
    }

#ifdef XRC
    if(NULL != mvdev.xrc_info) {

        /* free XRC SRQs */
        mv_srq_shared * xrc_srq_curr = mvdev.xrc_srqs;
        mv_srq_shared * xrc_srq_next = NULL;

        while(NULL != xrc_srq_curr) {
            ibv_destroy_srq(xrc_srq_curr->pool[0]->srq->srq);
            free(xrc_srq_curr->pool[0]->srq);
            free(xrc_srq_curr->pool[0]);
            xrc_srq_next = xrc_srq_curr->next;
            free(xrc_srq_curr);
            xrc_srq_curr = xrc_srq_next;
        }

        /* free XRC connections */
        for(i = 0; i < mvdev.xrc_info->uniq_hosts; i++) {
            if(MV_XRC_CONN_SHARED_ESTABLISHED == mvdev.xrc_info->connections[i].status
                    || MV_XRC_CONN_SHARED_CONNECTING == mvdev.xrc_info->connections[i].status) {
                mvdev_channel_xrc_shared * xrc_shared = 
                    mvdev.xrc_info->connections[i].xtra_xrc_next;
                ibv_destroy_qp(mvdev.xrc_info->connections[i].qp[0].qp);

                while(NULL != xrc_shared) {
                    ibv_destroy_qp(xrc_shared->qp[0].qp);
                    xrc_shared = xrc_shared->xtra_xrc_next;
                } 

            }   
        }   

        free(mvdev.xrc_info->connections);

        for(i = 0; i < mvdev.num_hcas; i++) {
            if(ibv_close_xrc_domain(mvdev.xrc_info->xrc_domain[i])) {
                fprintf(stderr, "Error destroying XRC Domain!\n");
            }   
            if(mvparams.xrcshared) {
                if(close(mvdev.xrc_info->fd[i])) {
                    fprintf(stderr, "Error closing XRC file!\n");
                }   
            }
        }   

        free(mvdev.xrc_info);
    }
#endif

    for(i = 0; i < mvdev.num_hcas; i++) {
        /* cancel the event thread */
        pthread_cancel(mvdev.hca[i].async_thread);
        pthread_join(mvdev.hca[i].async_thread, NULL);

        ibv_destroy_cq(mvdev.cq[i]);
        ibv_dealloc_pd(mvdev.hca[i].pd);
        ibv_close_device(mvdev.hca[i].context);
    }

    free(mvdev.processes);
    free(mvdev.processes_buffer);
    free(mvdev.hca);
    free(mvdev.lids);
    free(mvdev.qpns);
    free(mvdev.connections);
    free(mvdev.connection_ack_required);
    free(mvdev.pids);
    free(mvdev.my_name);

}


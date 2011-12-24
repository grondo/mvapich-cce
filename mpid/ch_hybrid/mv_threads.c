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


#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include "mpid.h"
#include "ibverbs_header.h"
#include "mv_dev.h"
#include "mv_priv.h"
#include "mv_buf.h"
#include "mv_packet.h"

void MV_Start_Threads() 
{
    int i;
    for(i = 0; i < mvdev.num_hcas; i++) {
        pthread_create(&(mvdev.hca[i].async_thread), NULL,
                (void *) async_thread, (void *) mvdev.hca[i].context);
    }
}

void async_thread(void *context)
{
    struct ibv_async_event event;
    int ret;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    while (1) {

        do {    
            ret = ibv_get_async_event((struct ibv_context *) context, &event);  
            if (ret && errno != EINTR) {
                fprintf(stderr, "Error getting event!\n");
            }           
        } while(ret && errno == EINTR);

        switch (event.event_type) {
            /* Fatal */
            case IBV_EVENT_PATH_MIG:
            case IBV_EVENT_PATH_MIG_ERR:
            case IBV_EVENT_DEVICE_FATAL:
            case IBV_EVENT_SRQ_ERR:
                error_abort_all(GEN_EXIT_ERR, "[%d] Got FATAL event %d\n",
                        mvdev.me, event.event_type);                                           break;

            case IBV_EVENT_CQ_ERR:
            case IBV_EVENT_QP_FATAL:
            case IBV_EVENT_QP_REQ_ERR:
            case IBV_EVENT_QP_ACCESS_ERR:
            case IBV_EVENT_COMM_EST:
            case IBV_EVENT_PORT_ACTIVE:
            case IBV_EVENT_SQ_DRAINED:
            case IBV_EVENT_PORT_ERR:
            case IBV_EVENT_LID_CHANGE:
            case IBV_EVENT_PKEY_CHANGE:
            case IBV_EVENT_SM_CHANGE:
            case IBV_EVENT_QP_LAST_WQE_REACHED:
                break;
            case IBV_EVENT_SRQ_LIMIT_REACHED:
                fprintf(stderr, "[%d] Received IBV_EVENT_SRQ_LIMIT_REACHED signal\n", mvdev.me);
                D_PRINT("Received IBV_EVENT_SRQ_LIMIT_REACHED signal\n\n");
                break;

            default:
                fprintf(stderr,
                        "[%d] Got unknown event %d ... continuing ...\n",
                        mvdev.me, event.event_type);
        }

        ibv_ack_async_event(&event);
    }
}





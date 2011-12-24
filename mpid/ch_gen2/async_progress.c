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
#include "viutil.h"
#include "viapriv.h"
#include "viapacket.h"
#include "queue.h"
#include "viaparam.h"
#include "vbuf.h"
#include <signal.h>
#include "sig_handler.h"

pthread_t async_progress_threadId;
pthread_t parent_threadId;
static void async_completion_thread();

int viadev_init_async_progress()
{
    struct sched_param param;

    pthread_attr_t attr;

    pthread_attr_init(&attr);

    param.sched_priority = sched_get_priority_max(viadev_async_schedule_policy);

    pthread_attr_setschedparam(&attr, &param);

    pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS);

    if(pthread_attr_setschedpolicy(&attr, viadev_async_schedule_policy)) {
        printf("pthread_attr_setschedpolicy failed\n");
         return 1;
     }

    parent_threadId = pthread_self();

    initSignalHandler();

    if(ibv_req_notify_cq(viadev.cq_hndl, 1)) {
        return 1;
    }

    pthread_attr_setinheritsched (&attr, PTHREAD_EXPLICIT_SCHED);

    if(pthread_create(&async_progress_threadId,
            &attr, (void *) async_completion_thread, NULL)) {
        return 1;
    }

    return 0;
}

static void async_completion_thread()
{
    int ret;
    struct ibv_comp_channel *ev_ch;
    struct ibv_cq *ev_cq;
    void *ev_ctx;

    /* This thread should be in a cancel enabled state */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    ev_ch = viadev.comp_channel;

    while(1) {
        pthread_testcancel();
        pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
        do {
            ret = ibv_get_cq_event(ev_ch, &ev_cq, &ev_ctx);

            if (ret && errno != EINTR) {
                error_abort_all(IBV_RETURN_ERR,
                        "Failed to get cq event: %d\n", ret);
            }

        } while (ret && errno == EINTR);

        pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

        if (ev_cq != viadev.cq_hndl) {
            error_abort_all(GEN_ASSERT_ERR, "Event in unknown CQ\n");
        }

        pthread_kill(parent_threadId, SIGUSR1);

        ibv_ack_cq_events(viadev.cq_hndl, 1);

        pthread_testcancel();

        pthread_testcancel();


        if (ibv_req_notify_cq(viadev.cq_hndl, 1)) {
            error_abort_all(IBV_RETURN_ERR,
                    "Couldn't request for CQ notification\n");
        }
    }
}

int viadev_finalize_async_progress()
{
    /* Cancel thread if active */
    if (pthread_cancel(async_progress_threadId)) {
        error_abort_all(GEN_ASSERT_ERR,
                "Failed to cancel async completion thread\n");
    }

    if (pthread_join(async_progress_threadId, NULL)) {
        error_abort_all(GEN_ASSERT_ERR,
                "Failed to join async completion thread\n");
    }

    return 0;
}



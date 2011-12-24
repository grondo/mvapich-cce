/*
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Berkeley Lab as part of MVICH.
 *
 * Authors: Bill Saphir      <wcsaphir@lbl.gov>
 *          Michael Welcome  <mlwelcome@lbl.gov>
 */

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
#include "viutil.h"
#include "viapriv.h"
#include "viapacket.h"
#include "queue.h"
#include "viaparam.h"
#include "mpid_smpi.h"

static void viutil_spinandwaitcq(void);
/*
 * MPID_DeviceCheck processes ALL completed descriptors. 
 * This means it handles ALL incoming packets as well (as 
 * takes care of marking send requests complete when data is
 * transferred -- still not sure if it actually does something here 
 * 
 * Each time it is
 * called, it processes ALL descriptors that have completed since
 * the last time it was called, even if it was called nonblocking. 
 * 
 * If the blocking form is called, and there are no packets waiting, 
 * it spins for a short time and then waits on the connection queue 
 * 
 * Most of the work of the VIA device is done in this routine. 
 *
 * As this routine is on the critical path, will have to work hard
 * to optimize it. Reducing function calls will be important. 
 */


/* 
 * The flowlist is a list of connections that have received new flow 
 * credits and may be able to make progress on pending rendezvous sends.
 * The flowlist is a singly-linked list of connections, linked by 
 * the "nextflow" field in the connection. In order to prevent loops, 
 * there is also a connection field "inflow" which is set to one
 * if it is in the flowlist and zero otherwise. It must be 
 * initialized to 0 and it only changed by the flow control 
 * logic in MPID_CheckDevice
 * 
 * We add a connection to the flowlist when either
 *  1) it gets new credits and there is a rendezvous in progress
 *  2) a rendezvous ack is received
 * 
 * Note: flowlist fields are touched only routines in this file. 
 */



static viadev_connection_t *flowlist;
#define PUSH_FLOWLIST(c) {                                          \
    if (0 == c->inflow) {                                           \
        c->inflow = 1;                                              \
        c->nextflow = flowlist;                                     \
        flowlist = c;                                               \
    }                                                               \
}

#define POP_FLOWLIST() {                                            \
    if (flowlist != NULL) {                                         \
        viadev_connection_t *_c;                                    \
        _c = flowlist;                                              \
        flowlist = _c->nextflow;                                    \
        _c->inflow = 0;                                             \
        _c->nextflow = NULL;                                        \
    }                                                               \
}

#if (defined(_IA64_) && !defined(_ICC_))
#define RMB()  asm volatile ("mf" ::: "memory");
#elif defined(_IA32_)
#define RMB()  asm volatile ("lfence" ::: "memory");
#elif defined(_X86_64_)
#define RMB()  asm volatile ("lfence" ::: "memory");
#elif defined(_EM64T_)
#define RMB()  asm volatile ("lfence" ::: "memory");
#elif defined(_PPC64_)
#define RMB()  asm volatile ("sync": : :"memory");
#else
#define RMB()
#endif                          /* defined(_IA64_ ... */

static void process_flowlist(void);

/*
 * Attached to each connection is a list of send handles that
 * represent rendezvous sends that have been started and acked but not
 * finished. When the ack is received, the send is placed on the list;
 * when the send is complete, it is removed from the list.  The list
 * is an "in progress sends" queue for this connection.  We need it to
 * remember what sends are pending and to remember the order sends
 * were acked so that we complete them in that order. This
 * prevents a situation where we receive an ack for message 1, block
 * because of flow control, receive an ack for message 2, and are able
 * to complete message 2 based on the new piggybacked credits.
 * 
 * The list head and tail are given by the shandle_head and
 * shandle_tail entries on viadev_connection_t and the list is linked
 * through the nexthandle entry on a send handle.  
 *
 * The queue is FIFO because we must preserve order, so we maintain
 * both a head and a tail. 
 *
 */

#define RENDEZVOUS_IN_PROGRESS(c, s) {                              \
    if (NULL == c->shandle_tail) {                                  \
        c->shandle_head = s;                                        \
    } else {                                                        \
        c->shandle_tail->nexthandle = s;                            \
    }                                                               \
    c->shandle_tail = s;                                            \
    s->nexthandle = NULL;                                           \
}

#define RENDEZVOUS_DONE(c) {                                        \
    c->shandle_head = c->shandle_head->nexthandle;                  \
        if (NULL == c->shandle_head) {                              \
            c->shandle_tail = NULL;                                 \
        }                                                           \
}

int MPID_DeviceCheck(MPID_BLOCKING_TYPE blocking)
{
    int ret1, ret2;

    int gotone = 0;

    D_PRINT("Enter DeviceCheck Blocking = %s",
         (blocking == MPID_NOTBLOCKING ? "FALSE" : "TRUE"));

    flowlist = NULL;

    /* process all waiting packets */
    ret1 = ret2 = 0;

    /* Check the fastest channel */
#ifdef _SMP_
    if (!disable_shared_mem && SMP_INIT) {
        if (MPI_SUCCESS == MPID_SMP_Check_incoming())
            gotone = 1;

    }
#endif

    if (gotone || blocking == MPID_NOTBLOCKING) {
        D_PRINT("Leaving DeviceCheck gotone = %d", gotone);
        return gotone ? MPI_SUCCESS : -1;       /* nonblock must return -1 */
    }

    viutil_spinandwaitcq();

    return MPI_SUCCESS;
}

/* add a function here directly instead of putting it in viutil.c */
static void viutil_spinandwaitcq(void)
{
    int ret = 1;

    while (1) {

#ifdef _SMP_
        if ((!disable_shared_mem) && SMP_INIT) {
            if (MPID_SMP_Check_incoming() == MPI_SUCCESS) {
                ret = 0;
                break;
            }
        }
#endif

        if (ret == 1)
            continue;

        /* error when comes here */
        error_abort_all(GEN_ASSERT_ERR,
                        "[%s:%d] Error in (%s:%d): ret=%d\n",
                        viadev.my_name, viadev.me, __FILE__, __LINE__,
                        ret);
        break;
    }
}


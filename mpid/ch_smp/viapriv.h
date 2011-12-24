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


#ifndef _VIAPRIV_H
#define _VIAPRIV_H

#include <pthread.h>
#include "viutil.h"
#include "viaparam.h"

#include "req.h"

#define VIADEV_MAX_EXECNAME (256)

/* One of these structures per VI. Pointer to this
 * structure is also cached on the VI itself for two-way
 * lookup 
 */

typedef struct _viadev_connection_t {

    int global_rank;
    int remote_credit;          /* how many vbufs I can consume on remote end. */
    int local_credit;           /* accumulate vbuf credit locally here */
    int preposts;               /* number of vbufs currently preposted */
    int initialized;            /* have all the initial buffers preposted */
    int send_wqes_avail;        /* send Q WQES available */

    int ext_sendq_size;

    /* These are not necessary for reliable connections, but we use
     * them anyway as a sanity check
     * They will become necessary when we handle unreliable connections
     */

    packet_sequence_t next_packet_expected;     /* for sequencing (starts at 1) */
    packet_sequence_t next_packet_tosend;       /* for sequencing (starts at 1) */

    int remote_cc;              /* current guess at remote sides
                                 * credit count */

    /* This field is used for managing preposted receives. It is the
     * number of rendezvous packets (r3/rput) expected to arrive for
     * receives we've ACK'd but have not been completed. In general we
     * can't prepost all of these vbufs, but we do prepost extra ones
     * to allow sender to keep the pipe full. As packets come in this
     * field is decremented.  We know when to stop preposting extra
     * buffers when this number goes to zero.
     */

    int rendezvous_packets_expected;

    /* these fields are used to remember data transfer operations
     * that are currently in progress on this connection. The 
     * send handle list is a queue of send handles representing
     * in-progress rendezvous transfers. It is processed in FIFO
     * order (because of MPI ordering rules) so there is both a head
     * and a tail. 
     *
     * The receive handle is a pointer to a single
     * in-progress eager receive. We require that an eager sender
     * send *all* packets associated with an eager receive before
     * sending any others, so when we receive the first packet of 
     * an eager series, we remember it by caching the rhandle
     * on the connection. 
     *
     */


    MPIR_SHANDLE *shandle_head; /* "queue" of send handles to process */
    MPIR_SHANDLE *shandle_tail;
    MPIR_RHANDLE *rhandle;      /* current eager receive "in progress" */

    /* these two fields are used *only* by MPID_DeviceCheck to 
     * build up a list of connections that have received new
     * flow control credit so that pending operations should be 
     * pushed. nextflow is a pointer to the next connection on the
     * list, and inflow is 1 (true) or 0 (false) to indicate whether
     * the connection is currently on the flowlist. This is needed
     * to prevent a circular list.
     */
    struct _viadev_connection_t *nextflow;
    int inflow;

    /* used to distinguish which VIA barrier synchronozations have
     * completed on this connection.  Currently, only used during
     * process teardown.
     */
    int barrier_id;

    int rdma_reads_avail;

    int    pending_r3_data;
    int    received_r3_data;

    viadev_packet_envelope coalesce_cached_out;
    viadev_packet_envelope coalesce_cached_in;

} viadev_connection_t;

enum {
	MPID_VIA_SEND = 1,
	MPID_VIA_RECV = 2,
	MPID_VIA_SEND_RDNV = 3, /*Required to use RDNV Send e.g. for MPI_Ssend*/
};

typedef struct cm_pending_request {
	struct MPIR_COMMUNICATOR *comm_ptr;
	void *buf;
	int len;
	int src_lrank;
	int tag;
	int context_id;
	int dest_grank;
/*	MPID_Msgrep_t msgrep;*/  
	MPI_Request request;
	int * error_code;

	int type;
	struct cm_pending_request * next;
}cm_pending_request;

typedef struct {
    int                 hca_port_active;

    pthread_t           async_thread;
    pthread_spinlock_t  srq_post_spin_lock;
    pthread_mutex_t     srq_post_mutex_lock;
    pthread_cond_t      srq_post_cond;
    uint32_t            srq_zero_post_counter;
    uint32_t            posted_bufs;

#ifdef USE_MPD_RING
    int boot_tb[2][2];          /* HCA LID and QP for the ring */
#endif

    uint32_t *ud_qpn_table;
    volatile int cm_new_connection;

    uint16_t            my_hca_lid;

    uint8_t             lmc;
    /* my HCA LID */
    
    uint16_t            *lid_table;
    /* Store HCA LID of all processes */

    uint32_t            *qp_table;
    /* Store HCA QP num of all processes */

    unsigned long       maxtransfersize;
    int np;
    /* number of processes total */
    int me;

    /* is device initialized */
    int initialized;

    /* my process rank */
    char *my_name;
    /* A string equivalent of node name */
    int global_id;
    /* global id of this parallel app */

    viadev_connection_t *connections;
    /* array of VIs connected to other processes */
    int barrier_id;
    /* Used for VIA barrier operations */

    int *pids;                    /* add for totalview */
    char **processes;
    char execname[VIADEV_MAX_EXECNAME];           /* add for totalview */

    char device_name[32];         /* Name of the IB device */

    char is_finalized;            /* Flag to indicate if device
                                     is finalized */

} viadev_info_t;

extern viadev_info_t viadev;

int power_two(int);
/* 
 * on 32 bit machines, we identify (for now) a handle by its address, using the 
 * address for immediate data. 
 * on 64 bit machines, we need to use the pointer index stuff since we don't
 * have 64 bits of immediate data. 
 * We will also need the index stuff when we implement reliability. 
 */

#define REQ_TO_ID(h) ((request_id_t)(h))
#define ID_TO_REQ(id)((union MPIR_HANDLE *)(id))

#define FLUSH_EXT_SQUEUE(_c) {                                      \
    if((_c)->send_wqes_avail && (_c)->ext_sendq_head) {             \
        viadev_ext_sendq_send(_c);                                  \
    }                                                               \
}

/* 
 * this should be a real function. Need to think carefully about
 * what has to be done. 
 * note model does MPID_UnpackMessageComplete, etc.
 */

#include <unistd.h>

#define SEND_COMPLETE(s) {                                          \
    s->is_complete = 1;                                             \
    if (s->finish != NULL) {                                        \
        s->finish(s);                                               \
    }                                                               \
    if (s->ref_count == 0) {                                        \
        switch (s->handle_type) {                                   \
            case MPIR_SEND:                                         \
            {                                                       \
                MPID_SendFree(s);                                   \
                break;                                              \
            }                                                       \
            case MPIR_PERSISTENT_SEND:                              \
            {                                                       \
                MPID_PSendFree(s);                                  \
                break;                                              \
            }                                                       \
            default:                                                \
                error_abort_all(GEN_EXIT_ERR, "SEND_COMPLETE invalid type\n");    \
        }                                                           \
    }                                                               \
}

#define RECV_COMPLETE(r) {                                          \
    r->is_complete = 1;                                             \
    r->s.MPI_ERROR = MPI_SUCCESS;                                   \
    r->s.count = r->len;                                            \
    if (r->finish != NULL) {                                        \
        r->finish(r);                                               \
    }                                                               \
    if (r->ref_count == 0) {                                        \
        switch (r->handle_type) {                                   \
            case MPIR_RECV:                                         \
            {                                                       \
                MPID_RecvFree(r);                                   \
                break;                                              \
            }                                                       \
            case MPIR_PERSISTENT_RECV:                              \
            {                                                       \
                MPID_PRecvFree(r);                                  \
                break;                                              \
            }                                                       \
            default:                                                \
                error_abort_all(GEN_EXIT_ERR, "RECV_COMPLETE invalid type\n");    \
        }                                                           \
    }                                                               \
}

#define MPID_CommInit(oldcomm,newcomm) MPI_SUCCESS
#define MPID_CommFree(oldcomm) MPI_SUCCESS

#endif                          /* _VIAPRIV_H */

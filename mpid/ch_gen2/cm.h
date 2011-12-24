
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

#ifndef MPID_CM_H
#define MPID_CM_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <malloc.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <infiniband/verbs.h>
#include <errno.h>
#include <assert.h>

typedef union {
    uint16_t      lid;  /* Native IB */
    union ibv_gid gid;  /* RDMA over Eth */
} lgid; /* Lid-Gid*/


typedef volatile enum MPICM_conn_state {
    MPICM_IB_NONE = 1,
    MPICM_IB_RC_PT2PT = 1 << 1,
    MPICM_IB_XRC_PENDING = 1 << 2,
}MPICM_conn_state;

typedef struct MPICM_ib_qp_attr{
    struct ibv_qp_init_attr rc_qp_init_attr;
    struct ibv_qp_attr rc_qp_attr_to_init;
    enum ibv_qp_attr_mask rc_qp_mask_to_init;
    struct ibv_qp_attr rc_qp_attr_to_rtr;
    enum ibv_qp_attr_mask rc_qp_mask_to_rtr;
    struct ibv_qp_attr rc_qp_attr_to_rts;
    enum ibv_qp_attr_mask rc_qp_mask_to_rts;
}MPICM_ib_qp_attr;

extern MPICM_ib_qp_attr cm_ib_qp_attr;
extern MPICM_conn_state *cm_conn_state;

/*Lock to protect access to conn_state or connection request*/
int MPICM_Lock();
int MPICM_Unlock();
int MPICM_Init_lock();

/*
MPICM_Init_UD
Initialize MPICM based on UD connection, need to be called before calling 
any of following interfaces, it will return a qpn for the established ud qp, 
mpi lib needs to exchange the ud qpn with other procs
*/
int MPICM_Init_UD(uint32_t *ud_qpn);

/*
MPICM_Connect_UD
Provide connect information to UD
*/
int MPICM_Connect_UD(uint32_t *qpns, lgid *lgids);

/*
MPICM_Connect_req
Request to establish a type of connection.
*/
int MPICM_Connect_req(int peer_rank);

/*
MPICM_Finalize_UD
Finalize cm, clean supporting thread
*/
int MPICM_Finalize_UD();

/*
MPICM_Server_connection_establish
*/
int MPICM_Server_connection_establish(int peer_rank);

/*
 * Used only by NR
 * Send reconnect request
 */
int MPICM_Reconnect_req(int peer_rank);

#define CM_ERR(args...)  \
    fprintf(stderr, "[Rank %d][%s: line %d]", viadev.me ,__FILE__, __LINE__); \
    fprintf(stderr, args); \
    fprintf(stderr, "\n");

#ifdef CM_DEBUG
#define CM_DBG(args...)  \
    fprintf(stderr, "[Rank %d][%s: line %d]", viadev.me ,__FILE__, __LINE__); \
    fprintf(stderr, args); \
    fprintf(stderr, "\n");
#define CM_DBG_MPI(args...) \
    fprintf(stderr, "[Rank %d][%s: line %d]", viadev.me ,__FILE__, __LINE__); \
    fprintf(stderr, args); \
    fprintf(stderr, "\n");
#else
#define CM_DBG(args...)
#define CM_DBG_MPI(args...)
#endif

#ifdef XRC

typedef struct _xrc_hash {
    struct _xrc_hash    *next;
    struct ibv_qp       *qp;

    uint32_t            xrc_qp_dst;     /* Destination rank of original qp */
    uint32_t            host;
} xrc_hash_t;
#define xrc_hash_s (sizeof (xrc_hash_t))

#define XRC_HASH_SIZE       1024    /* Make this dynamic? */
#define XRC_HASH_MASK       (XRC_HASH_SIZE - 1)

void clear_xrc_hash (void);
int compute_xrc_hash (uint32_t v);
void add_qp_xrc_hash (int peer, struct ibv_qp *qp);
void cm_activate_xrc_qp_reuse (int peer_rank);
#endif /* XRC */

#endif  /* MPID_CM_H */

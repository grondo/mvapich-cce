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

#ifndef _COLLUTILS_H_
#define _COLLUTILS_H_

#if (defined(VIADEV_RPUT_SUPPORT) || defined(VIADEV_RGET_SUPPORT))

#include "ibverbs_header.h"
#include "vbuf.h"
#include "dreg.h"
#include "mpid_bind.h"
#include "comm.h"

/* Utility macros */

#define CHECK_ALLOC(ptr) {                                          \
    if(NULL == ptr) {                                               \
        fprintf(stderr, "No memory at line %d in file %s\n",        \
                __LINE__, __FILE__);                                \
        exit(-1);                                                   \
    }                                                               \
}

#define ALLGATHER_HEADER_SIZE (2*sizeof(char))

#define ALLGATHER_RDMA_FLAG (112)

#define ALLGATHER_HEAD_FLAG (113)
#define ALLGATHER_TAIL_FLAG (114)

/* This flag is to indicate the arrival of
 * a RDMA message */
#define ALLTOALL_RDMA_FLAG (111)

/* Space reserved in the collective buffer for keeping flags */
#define COLL_FLAG_SPACE     (32)

/* Maximum number of DE processes */
#define MAX_DE_NODES        (viadev.np)

/* Space for the address & remote keys to be sent */
#define COLL_ADDR_SEND_SPACE ((sizeof(aint_t)+sizeof(uint32_t) \
        + sizeof(char)))

#define COLL_ACK_SPACE      (2*MAX_DE_NODES*(sizeof(char)))

/* The Direct Eager address buffer should hold the destination
 * addresses of the receive buffers for total N processes
 * participating in the Alltoall.
 */
#define COLL_ADDR_RECV_SPACE ((2 * MAX_DE_NODES * (sizeof(aint_t) +  \
            sizeof(uint32_t) + sizeof(char))))

#define COLL_RDMA_FLAG_SPACE (1)

#define ALLGATHER_THRESHOLD (4096)

#define MAX_ALLGATHER_NODES (viadev.np)

#define ALLGATHER_BUF_SIZE (2*ALLGATHER_THRESHOLD + MAX_ALLGATHER_NODES*ALLGATHER_HEADER_SIZE)

#define COLL_SINGLE_BUF_SIZE \
(ALLGATHER_BUF_SIZE + COLL_FLAG_SPACE + COLL_ADDR_RECV_SPACE)

/* Enough space for 3 back-to-back RDMA allgather buffers */
#define ALLGATHER_COLL_BUF_SIZE (3*COLL_SINGLE_BUF_SIZE + COLL_ADDR_SEND_SPACE + \
		COLL_RDMA_FLAG_SPACE)


#define BARRIER_SPACE   (viadev.np * 4)

#define COLL_BUF_SIZE       (COLL_FLAG_SPACE + COLL_ADDR_SEND_SPACE \
        + COLL_ACK_SPACE + COLL_ADDR_RECV_SPACE + 1024 +BARRIER_SPACE + 1024 + ALLGATHER_COLL_BUF_SIZE)

#define BARRIER_OFFSET (COLL_FLAG_SPACE + COLL_ADDR_SEND_SPACE \
        + COLL_ACK_SPACE + COLL_ADDR_RECV_SPACE + 1024)
#define ADDR_EXCHANGE_TAG   (101)

#define ALLGATHER_OFFSET (BARRIER_OFFSET + BARRIER_SPACE + 1024) 

/* Collective structures */
struct R_Collbuf {
    void *buf;
    uint32_t rkey;
};

struct L_Collbuf {
    void *buf;
    struct ibv_mr* mr;
};

struct DE_Remote_Node {
    /* Here is where this node's address
     * will be received */
    void *recv_addr[2];

    /* Here is where I will write to the remote
     * node's memory */
    void *dest_addr[2];

    /* This tells me if this remote node is 
     * done with me or not */
    void *done[2];

    /* This is where I write to let the remote
     * node know that I am done with it */
    void *r_done[2];

    struct ibv_mr mr;
};

struct Direct_Eager {
    int alternate;
    struct DE_Remote_Node *node;
};


struct Allgather_Peer{
	/* Here is where this node's address
	 * will be received */
	char *recv_addr[3];

	/* Here is where I will write my address to the remote
	 * node's memory */
	char *dest_addr[3];

	/* This tells me if this remote node is 
	 * done with me or not */
	char *done[3];

	/* This is where I write to let the remote
	 * node know that I am done with it */
	char *r_done[3];

    struct ibv_mr mr;
};

struct Allgather
{
	char *rdma_flag;
	int rdma_flag_size;

	struct Allgather_Peer *peers;

	char *buf_hndl_addr;
	int buf_hndl_addr_len;

	aint_t *rdma_recv_addr;
	int rdma_recv_addr_len;

	int dimensions;
	int alternate;
};

struct Alltoall {
    void *rdma_flag;
    int rdma_flag_size;
    struct Direct_Eager *de;

    void *rdma_recv_addr;
    int rdma_recv_addr_len;

    void *buf_hndl_addr;
    int buf_hndl_addr_len;
};


struct Collbuf {
    struct R_Collbuf *r_coll;
    struct L_Collbuf *l_coll;

    /* for alltoall support */
    struct Alltoall *atoa;

    /* for allgather support */
    struct Allgather *allgather;


    /*for rdma barrier */
    int rdma_barrier_id;
    int togle;
};

struct Coll_Addr_Exch {
    void *buf;
    uint32_t rkey;
};

/* Prototypes for collective utilities */

int log_2(int);
void copy_recv_addr(void *, struct Collbuf *, struct ibv_mr*);
void copy_recv_addr_allgather(void *, struct Collbuf *, struct ibv_mr*);
void coll_rdma_write(int, void *, int, struct ibv_mr*, void *, uint32_t);
void copy_to_rdma_buf(void* , void* , int , int , int);
void Next_Power_of_Degree(int, int, int *);
int intra_RDMA_barrier(struct MPIR_COMMUNICATOR *comm);
void coll_all_send_complete(int dest);

#endif                          /* VIADEV_RPUT_SUPPORT || VIADEV_RGET_SUPPORT */

#endif                          /* _COLLUTILS_H_ */

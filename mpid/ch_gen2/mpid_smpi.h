
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

/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
 */


#include "req.h"
#include "mpid.h"
extern int SMP_INIT;
/* Include the appropriate constants file */
#include "ibverbs_const.h"

#if defined(_IA32_)
    #define SMP_EAGERSIZE       (8)       /* 8 Kbytes */
    #define SMPI_LENGTH_QUEUE   (128)
    #define SMP_BATCH_SIZE      (8)
    #define SMP_NUM_SEND_BUFFER (128)
    #define SMP_SEND_BUF_SIZE   (8192)
#elif defined(_IA64_)
    #define SMP_EAGERSIZE       (8)       /* 8 Kbytes */
    #define SMPI_LENGTH_QUEUE   (128)
    #define SMP_BATCH_SIZE      (8)
    #define SMP_NUM_SEND_BUFFER (128)
    #define SMP_SEND_BUF_SIZE   (8192)
#elif defined(_EM64T_)
    #define SMP_EAGERSIZE       (32)        /* 32 Kbytes */
    #define SMPI_LENGTH_QUEUE   (128)
    #define SMP_BATCH_SIZE      (8)
    #define SMP_NUM_SEND_BUFFER (128)
    #define SMP_SEND_BUF_SIZE   (8192)
#elif defined(_X86_64_) && defined(_AMD_QUAD_CORE_)
    #define SMP_EAGERSIZE       (32)       /* 32 Kbytes */
    #define SMPI_LENGTH_QUEUE   (128)
    #define SMP_BATCH_SIZE      (32)
    #define SMP_NUM_SEND_BUFFER (128)
    #define SMP_SEND_BUF_SIZE   (8192)
#elif defined(_X86_64_)
    #define SMP_EAGERSIZE       (8)       /* 8 Kbytes */
    #define SMPI_LENGTH_QUEUE   (32)
    #define SMP_BATCH_SIZE      (2)
    #define SMP_NUM_SEND_BUFFER (128)
    #define SMP_SEND_BUF_SIZE   (8192)
#elif defined(_PPC64_)
    #define SMP_EAGERSIZE       (8)       /* 8 Kbytes */
    #define SMPI_LENGTH_QUEUE   (128)
    #define SMP_BATCH_SIZE      (8)
    #define SMP_NUM_SEND_BUFFER (128)
    #define SMP_SEND_BUF_SIZE   (8192)
#else
#error "No architecture defined !!"
#endif

#define PID_CHAR_LEN 22

#define MPID_PKT_LAST_MSG MPID_PKT_REQUEST_SEND
typedef enum { MPID_PKT_SHORT = 1,
    MPID_PKT_REQUEST_SEND = 2,
    MPID_PKT_OK_TO_SEND = 3,
    MPID_PKT_ANTI_SEND = 4,
    MPID_PKT_ANTI_SEND_OK = 5,
    MPID_PKT_DONE_SEND = 6,
    MPID_PKT_DO_GET = 7,
    MPID_PKT_CONT_GET = 8,
    MPID_PKT_DONE_GET = 9,
    MPID_PKT_FLOW = 10,
    MPID_PKT_PROTO_ACK = 11,
    MPID_PKT_ACK_PROTO = 12
} MPID_Pkt_t;
#define MPID_PKT_IS_MSG(mode) ((mode) <= MPID_PKT_LAST_MSG)

/* the maximum number of nodes in the job */
#define SMPI_INITIAL_SEND_FIFO 1024

#define SMPI_ALIGN_MASK 8

#if defined(_IA32_)

#define SMPI_CACHE_LINE_SIZE 64
#define SMPI_ALIGN(a)                                               \
((a + SMPI_CACHE_LINE_SIZE + 7) & (-SMPI_ALIGN_MASK))

#elif defined(_IA64_)

#define SMPI_CACHE_LINE_SIZE 128
#define SMPI_ALIGN(a)                                               \
((a + SMPI_CACHE_LINE_SIZE + 7) & (-SMPI_ALIGN_MASK))

#elif defined(_X86_64_)
#define SMPI_CACHE_LINE_SIZE 128
#define SMPI_ALIGN(a)                                               \
((a + SMPI_CACHE_LINE_SIZE + 7) & (-SMPI_ALIGN_MASK))

#else

#define SMPI_CACHE_LINE_SIZE 64
#define SMPI_ALIGN(a) (a +SMPI_CACHE_LINE_SIZE)

#endif

#define SMPI_MAX_INT ((unsigned int)(-1))

/* Macros for flow control and rqueues management */
#define SMPI_TOTALIN(sender,receiver)                               \
smpi_shmem->rqueues_params[sender][receiver].msgs_total_in

#define SMPI_TOTALOUT(sender,receiver)                              \
smpi_shmem->rqueues_flow_out[receiver][sender].msgs_total_out

#define SMPI_CURRENT(sender,receiver)                               \
smpi_shmem->rqueues_params[receiver][sender].current

#define SMPI_NEXT(sender,receiver)                                  \
smpi_shmem->rqueues_params[sender][receiver].next

#define SMPI_FIRST(sender,receiver)                                 \
smpi_shmem->rqueues_limits[receiver][sender].first

#define SMPI_LAST(sender,receiver)                                  \
smpi_shmem->rqueues_limits[receiver][sender].last


/* packets definition */
#define SMPI_PKT_BASIC                                              \
  short mode;             /* Contains MPID_Pkt_t */                 \
  int lrank;            /* Local rank in sending context */       \
  unsigned int context_id;   /* Context_id */                       \
  int tag;                   /* tag is full sizeof(int) */          \
  int len;                   /* Length of DATA */                   \
  int sequence_id;

/* This is the minimal message packet */
typedef struct {
    SMPI_PKT_BASIC void *send_id;
} SMPI_PKT_HEAD_T;

/* Short messages are sent eagerly (unless Ssend) */
typedef struct {
    SMPI_PKT_BASIC void *send_id;
} SMPI_PKT_SHORT_T;


/* Long messages are sent rendez-vous if not directcopy */
typedef struct {
    SMPI_PKT_BASIC void *send_id;
    void *recv_id;
    int address;
} SMPI_PKT_CONT_GET_T;

typedef struct {
    SMPI_PKT_BASIC void *send_id;
    void *address;              /* Location of data ON SENDER */
} SMPI_PKT_GET_T;

/* RNDV_request packet */
typedef struct {
    SMPI_PKT_BASIC void *send_id;
    void *recv_id;
     int address;
} SMPI_PKT_RNDV_T;

/* Cancel packet */
typedef struct {
    SMPI_PKT_BASIC void *send_id;
    int cancel;                 /* set to 1 if msg was cancelled - 
                                   0 otherwise */
} SMPI_PKT_ANTI_SEND_T;

/* all the differents types of packets */
typedef union _SMPI_PKT_T {
    SMPI_PKT_HEAD_T head;
    SMPI_PKT_SHORT_T short_pkt;
    SMPI_PKT_CONT_GET_T cont_pkt;
    SMPI_PKT_GET_T get_pkt;
    SMPI_PKT_RNDV_T rndv_pkt;
    SMPI_PKT_ANTI_SEND_T cancel_pkt;
    char pad[SMPI_CACHE_LINE_SIZE];
} SMPI_PKT_T;

/* structure for a buffer in the sending buffer pool */
typedef struct send_buf_t {
    int myindex;
    int next;
    volatile int busy;
    int len;
    int has_next;
    int msg_complete;
    char buf[SMP_SEND_BUF_SIZE];
} SEND_BUF_T;

/* send_requests fifo, to stack the send in case of starvation of buffer
   in the shared area memory */
struct smpi_send_fifo_req {
    void *data;
    void *isend_data;
    struct _MPIR_SHANDLE *shandle;
    struct smpi_send_fifo_req *next;
    int len;
    int grank;
    int is_data;
};

/* management informations */
struct smpi_var {
    void *mmap_ptr;
    void *send_buf_pool_ptr;  
    struct smpi_send_fifo_req *send_fifo_head;
    struct smpi_send_fifo_req *send_fifo_tail;
    unsigned int send_fifo_queued;
    unsigned int my_local_id;
    unsigned int num_local_nodes;
    unsigned int *local_nodes;  /* changed by wuj to remove the hard limit */
    short int only_one_device; /* to see if all processes are on one physical node */

    unsigned int *l2g_rank;
    int available_queue_length;
    int pending;
    int fd;
    int fd_pool;
};

typedef struct {
    volatile unsigned int current;
    volatile unsigned int next;
    volatile unsigned int msgs_total_in;
} smpi_params;

typedef struct {
    volatile unsigned int msgs_total_out;
    char pad[SMPI_CACHE_LINE_SIZE - 4];
} smpi_rqueues;

typedef struct {
    volatile unsigned int first;
    volatile unsigned int last;
} smpi_rq_limit;

/* the shared area itself */
struct shared_mem {
    volatile int *pid;   /* use for initial synchro */
    /* receive queues descriptors */
    smpi_params **rqueues_params;

    /* rqueues flow control */
    smpi_rqueues **rqueues_flow_out;

    smpi_rq_limit **rqueues_limits;

    int pad2[SMPI_CACHE_LINE_SIZE];

    /* the receives queues */
    char *pool;
};

/* to be initialized */
struct shared_buffer_pool {
    int free_head;
    int *send_queue;    
    int *tail;
};

extern struct smpi_var smpi;
extern struct shared_mem *smpi_shmem;

extern int smpi_net_lookup(void);
extern int MPID_SMP_Check_incoming(void);
extern void smpi_init(void);
extern void smpi_finish(void);
extern int MPID_SMP_Eagerb_unxrecv_start_short(MPIR_RHANDLE *,
                                                      MPIR_RHANDLE *);
int MPID_SMP_Eagerb_isend_short(void *, int, int, int, int,
                                              int, MPID_Msgrep_t,
                                              MPIR_SHANDLE *);
int MPID_SMP_Rndvn_isend ( void *, int, int, int, int, int,
                           MPID_Msgrep_t, MPIR_SHANDLE * );
int MPID_SMP_Rndvn_save ( MPIR_RHANDLE *, int, void *);
int MPID_SMP_Rndvn_unxrecv_posted ( MPIR_RHANDLE *, void * );
int MPID_SMP_Eagerb_send_short (void *, int len, int, int, int, int,
                                MPID_Msgrep_t, MPIR_SHANDLE *);

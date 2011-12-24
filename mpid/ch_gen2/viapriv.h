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
 /* Copyright (c) 2008, Mellanox Technologis. All rights reserved. */


#ifndef _VIAPRIV_H
#define _VIAPRIV_H

#include <pthread.h>

#include "calltrace.h"
#include "viaparam.h"

#include "ibverbs_header.h"

#include "vbuf.h"
#include "req.h"

#ifdef MCST_SUPPORT
#include "bcast_info.h"
#endif

#include "cm.h"


#define MAX_NUM_HCA 8

/* structure for tracking the collective buffer pinning*/
typedef struct reg_entry reg_entry;

struct reg_entry{
    void* buf;
    int valid;
    struct ibv_mr *mem_hndl;
    reg_entry* next;
};

/*
 * For setting up communication, use discriminators 
 * that include both the global id and rank. If global 
 * id is unique, or almost unique, this should be good enough. 
 */


/*
 * This structure is used to implement a vbuf backlog queue
 * for posting packet sends.  If no remote credits are available
 * on a connection at the time of the packet send, it will be
 * put on the end of the queue.  As credits become available
 * the packets on the queue are sent in the order received.
 */
typedef struct _viadev_backlog_queue_t {
    int len;                    /* length of backlog queue */
    vbuf *vbuf_head;            /* head of backlog queue */
    vbuf *vbuf_tail;            /* tail of backlog queue */
} viadev_backlog_queue_t;


#ifdef ADAPTIVE_RDMA_FAST_PATH

#define PRIMARY_FLAG (7777777)
#define SECONDARY_FLAG (88888888)

#define NORMAL_VBUF_FLAG (222)
#define RPUT_VBUF_FLAG (333)
#define RGET_VBUF_FLAG (444)

#define FREE_FLAG (0)
#define BUSY_FLAG (1)

#define IS_BUSY_VBUF(v) (v->padding != FREE_FLAG)

#define IS_NORMAL_VBUF(v) (v->padding == NORMAL_VBUF_FLAG)

#define IS_RPUT_VBUF(v) (v->padding == RPUT_VBUF_FLAG)

#endif

#define VIADEV_MAX_EXECNAME (256)

#ifdef XRC
typedef struct _viadev_conn_queue 
{
    int                         rank;
    struct _viadev_conn_queue   *next;
} viadev_conn_queue_t;

#define viadev_conn_queue_s (sizeof (viadev_conn_queue_t))
#endif 

struct ack_list {
    vbuf sentinel;
    int size;
};

typedef struct ack_list ack_list;

/* the list will keep rndv messages in process */
struct rndv_list {
    MPIR_RHANDLE sentinel;
    int size;
};

typedef struct rndv_list rndv_list;

/* One of these structures per VI. Pointer to this
 * structure is also cached on the VI itself for two-way
 * lookup 
 */

typedef struct _viadev_connection_t {

    struct ibv_qp *vi;

    int global_rank;
    int remote_credit;          /* how many vbufs I can consume on remote end. */
    int local_credit;           /* accumulate vbuf credit locally here */
    int preposts;               /* number of vbufs currently preposted */
    int initialized;            /* have all the initial buffers preposted */
    int send_wqes_avail;        /* send Q WQES available */
    struct vbuf *ext_sendq_head;        /* queue of sends which didn't fit on send Q */
    struct vbuf *ext_sendq_tail;        /* queue of sends which didn't fit on send Q */

    int ext_sendq_size;

    /* These are not necessary for reliable connections, but we use
     * them anyway as a sanity check
     * They will become necessary when we handle unreliable connections
     */

    packet_sequence_t next_packet_expected;     /* for sequencing (starts at 1) */
    packet_sequence_t next_packet_tosend;       /* for sequencing (starts at 1) */

    int remote_cc;              /* current guess at remote sides
                                 * credit count */
    /* the backlog queue for this connection. */
    viadev_backlog_queue_t backlog;

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
#ifdef MEMORY_RELIABLE
    packet_sequence_t max_seq_id_acked;
    vbuf* crc_queue;
    vbuf *ofo_head;
    packet_sequence_t max_datapkt_seq;
#endif


    int rdma_reads_avail;
    struct vbuf* ext_rdma_read_head;
    struct vbuf* ext_rdma_read_tail;

    int    pending_r3_data;
    int    received_r3_data;

#if (defined(MCST_SUPPORT) || defined(ADAPTIVE_RDMA_FAST_PATH))

#ifdef ADAPTIVE_RDMA_FAST_PATH

    void *RDMA_send_buf_orig;
    void *RDMA_recv_buf_orig;

    /* counting eager start packets to calculate
     * a threshold before upgrading the connection
     * to use the FAST RDMA eager buffers.
     */
    int eager_start_cnt;

    void *RDMA_send_buf_DMA;
    void *RDMA_recv_buf_DMA;

    /* RDMA buffers for sending */
    struct vbuf *RDMA_send_buf;

    /* RDMA buffers for receive */
    struct vbuf *RDMA_recv_buf;

    /* RDMA buffers needs to be registered */
    struct ibv_mr *RDMA_send_buf_hndl;
    struct ibv_mr *RDMA_recv_buf_hndl;

    /* RDMA buffer on the remote side */
    uint32_t remote_RDMA_buf_hndl;
    struct vbuf *remote_RDMA_buf;

    /* current flow control credit accumulated for remote side */
    int rdma_credit;

    /* pointer to the head of free send buffers */
    /* this pointer advances when packets are sent */
    int phead_RDMA_send;

    /* pointer to the tail of free send buffers 
     * no buffers available if head == tail */
    /* this pointer advances when we receive more credits */
    int ptail_RDMA_send;

    /* pointer to the head of free receive buffers 
     * this is also where we should poll for incoming
     * rdma write messages */
    /* this pointer advances when we receive packets */
    int p_RDMA_recv;

    /* tail of recv free buffer */
    /* last free buffer at the receive side */
    int p_RDMA_recv_tail;

    /* we must make sure that a completion entry
     * is generated once in a while 
     * this number of be less than max outstanding WQE */
    int num_no_completion;

#endif

    /* flag indicating we have received remote RDMA memory
     * address and handle for this connection */
    int remote_address_received;

#if !defined(DISABLE_HEADER_CACHING) && \
    defined(ADAPTIVE_RDMA_FAST_PATH)
    /* cache part of the packet header to reduce eager message size */
    viadev_packet_eager_start cached_outgoing;
    viadev_packet_eager_start cached_incoming;
#ifdef VIADEV_DEBUG    
    int cached_hit;
    int cached_miss;
#endif
#endif

    /* anything else ?? */

#endif /* MCST_SUPPORT || ADAPTIVE_RDMA_FAST_PATH */

    viadev_packet_envelope coalesce_cached_out;
    viadev_packet_envelope coalesce_cached_in;

    uint32_t max_inline;

#ifdef XRC
    int                 xrc_flags;
    int                 xrc_qp_dst;
    viadev_conn_queue_t *xrc_conn_queue;
#endif
    /* NR */
    ack_list waiting_for_ack; /* list of vbufs that are waiting for software ack */
    rndv_list rndv_inprocess; 
    packet_sequence_t pending_acks;  /* pending acks */

    int qp_status;
    int progress_recov_mode;
    char in_restart;
    int was_connected;
} viadev_connection_t;

#ifdef XRC
#define     XRC_FLAG_NONE                    0
#define     XRC_FLAG_START_RDMAFP   0x00000001
#define     XRC_FLAG_DIRECT_CONN    0x00000002
#define     XRC_FLAG_INDIRECT_CONN  0x00000004
#define     XRC_FLAG_NEW_QP         0x00000008

#endif 

#define XRC_QP_NEW      0
#define XRC_QP_REUSE    1

#ifdef ADAPTIVE_RDMA_FAST_PATH
int fast_rdma_ok(viadev_connection_t * c, int data_size,
        int is_eager_packet);
void release_recv_rdma(viadev_connection_t * c, vbuf * v);
void post_fast_rdma_with_completion(viadev_connection_t * c, int len);
int poll_rdma_buffer(void **vbuf_addr, int *out_of_order);
void viadev_incoming_rdma_address(vbuf * v, viadev_connection_t * c,
                                  viadev_packet_rdma_address * header);

#ifndef DISABLE_HEADER_CACHING
int search_header_cache(viadev_connection_t * c,
                        viadev_packet_eager_start * h);
#endif

#endif

#ifdef MCST_SUPPORT

typedef struct bcast_info_t {
    void *ack_buffer;
    void *ack_region;
    struct ibv_mr *ack_mem_hndl;
    void *remote_add[MAX_PROCS];
    int remote_rkey[MAX_PROCS];
    vbuf **co_buf;
    int *ack_cnt;
    int *msg_cnt;
    int ud_pkt;
    int Bcnt[MAX_PROCS];
    double **time_out;
    int *win_head;
    int *win_tail;
    int *buf_head;
    int *buf_tail;
    int *buf_credits;           /*credits of co_roots */
    int *buf_updates;           /*freed buff count..to be sent */
    int sbuf_head;              /*acks at the sender side*/
    int sbuf_tail;
    int **recent_ack;           /*latest ack for a co-root from a node*/
    int *min_ack;
    int *is_full;
    int ack_full;
    int init_called;
    int bcast_called;
} bcast_info_t;

#endif

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

#ifdef XRC
typedef struct _xrc_queue {
    struct _xrc_queue *next;
    struct _xrc_queue *prev;
    
    vbuf    *v;
    int     src_rank;
} xrc_queue_t;
#define xrc_queue_s (sizeof (xrc_queue_t))

typedef struct {
    int                     fd;
    struct ibv_xrc_domain   *xrc_domain;
} xrc_info_t;
#define xrc_info_s sizeof (xrc_info_t)

#endif /* XRC */

typedef struct {
    struct ibv_device   *nic;
    /* single NIC */

    struct ibv_device_attr dev_attr;
    struct ibv_port_attr port_attr;

    struct ibv_context  *context;
    /* HCA context */
    
    struct ibv_port_attr hca_port;
    /* Port number */
    int                 hca_port_active;

    struct ibv_pd       *ptag;
    /* single protection tag for all memory registration */

    struct ibv_qp       **qp_hndl;
    /* Array of QP handles for all connections */

    struct ibv_cq       *cq_hndl;
    /* one cq for both send and recv */
    struct ibv_srq      *srq_hndl;

    pthread_t           async_thread;
    pthread_spinlock_t  srq_post_spin_lock;
    pthread_mutex_t     srq_post_mutex_lock;
    pthread_cond_t      srq_post_cond;
    uint32_t            srq_zero_post_counter;
    uint32_t            posted_bufs;

    cm_pending_request ** pending_req_head; 
    cm_pending_request ** pending_req_tail; 
    uint32_t *ud_qpn_table;
    volatile int cm_new_connection;
    /* my HCA LID/GID */
    lgid             my_hca_id;
    /* Store HCA LID of all processes */
    lgid            *lgid_table;

    uint8_t             lmc;

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
#ifdef VIADEV_HAVE_RDMA_LIMIT
    int outstanding_rdmas;
    /* number of outstanding rdma ops */
#endif

    viadev_connection_t *connections;
    /* array of VIs connected to other processes */
    int barrier_id;
    /* Used for VIA barrier operations */

#ifdef ADAPTIVE_RDMA_FAST_PATH
    viadev_connection_t **RDMA_polling_group;
    int  RDMA_polling_group_size;

#ifndef DISABLE_HEADER_CACHING
    viadev_packet_eager_start match_hdr;
    /* This header is just a dummy header which is filled
     * before a match is made */
#endif
#endif

    /* FOr UD set up */
#ifdef MCST_SUPPORT
    struct ibv_qp *ud_qp_hndl;  /*  UD QP handles */
    struct ibv_cq *ud_scq_hndl;
    struct ibv_cq *ud_rcq_hndl;
    struct ibv_ah_attr av;
    struct ibv_ah* av_hndl;
    union ibv_gid mcg_dgid;
    uint16_t mclid;

    bcast_info_t bcast_info;
#endif

    struct ibv_comp_channel *comp_channel;

    int *pids;                    /* add for totalview */
    char **processes;
    char execname[VIADEV_MAX_EXECNAME]; /* add for totalview */

    struct ibv_recv_wr *array_recv_desc;
    char device_name[32];         /* Name of the IB device */

    reg_entry* coll_comm_reg;

    char is_finalized;            /* Flag to indicate if device
                                     is finalized */

    int num_connections;
 
#ifdef XRC
    uint32_t        *srqn;
    xrc_info_t      *xrc_info;
    int             *hostids; 
    xrc_queue_t     *req_queue;
    unsigned int    *def_hcas;
#endif
      
} viadev_info_t;

extern viadev_info_t viadev;

void viadev_init_backlog_queue(viadev_connection_t * c);
void viadev_backlog_send(viadev_connection_t * c);

/*
 * Function prototypes. All functions used internally by the VIA device. 
 */


void viadev_check_communication(void);

void viadev_barrier(void);

void viadev_post_rdmaread(viadev_connection_t * c, vbuf * v);
void viadev_ext_rdma_read_queue(viadev_connection_t * c, vbuf * v);
void viadev_ext_rdma_read_start(viadev_connection_t * c);

void viadev_post_send(viadev_connection_t * c, vbuf * v);

void viadev_post_rdmawrite(viadev_connection_t * c, vbuf * v);

void viadev_post_recv(viadev_connection_t * c, vbuf * v);

void viadev_ext_sendq_queue(viadev_connection_t * c, vbuf * v);
void viadev_ext_sendq_send(viadev_connection_t * c);

void viadev_copy_unexpected_handle_to_user_handle(MPIR_RHANDLE * rhandle,
                                                  MPIR_RHANDLE *
                                                  unexpected,
                                                  int *error_code);

void viadev_recv_r3(MPIR_RHANDLE * rhandle);
void viadev_recv_rput(MPIR_RHANDLE * rhandle);
void viadev_recv_rget(MPIR_RHANDLE * rhandle);

int viadev_calculate_vbufs_expected(int nbytes,
                                    viadev_protocol_t protocol);

int viadev_eager_ok(int len, viadev_connection_t * c);

void viadev_rendezvous_reply(MPIR_RHANDLE * rhandle);

void viadev_prepost_for_rendezvous(viadev_connection_t * connection,
                                   int vbufs_expected);
void viadev_process_recv(void *vbuf);
void viadev_process_send(void *vbuf);


void viadev_incoming_rendezvous_start(vbuf * v,
                                      viadev_connection_t * c,
                                      viadev_packet_rendezvous_start *
                                      header);

void viadev_incoming_rendezvous_reply(vbuf * v,
                                      viadev_connection_t * c,
                                      viadev_packet_rendezvous_reply *
                                      header);

void viadev_incoming_r3_data(vbuf * v,
                             viadev_connection_t * c,
                             viadev_packet_r3_data * header);

void viadev_incoming_rput_finish(vbuf * v,
                                 viadev_connection_t * c,
                                 viadev_packet_rput_finish * header);
void viadev_rput(viadev_connection_t * c, vbuf * v, void *local_address,
                 uint32_t local_memhandle, void *remote_address,
                 uint32_t remote_memhandle, int nbytes);
void viadev_rput_finish(MPIR_SHANDLE * s);

void viadev_r3_ack(viadev_connection_t *c);

void viadev_incoming_rget_finish(vbuf * v,
                                 viadev_connection_t * c,
                                 viadev_packet_rget_finish * header);
/* can leave it blank */
void viadev_rget(viadev_connection_t * c, vbuf * v, void *local_address,
                 uint32_t local_memhandle, void *remote_address,
                 uint32_t remote_memhandle, int nbytes);

void viadev_rget_finish(MPIR_RHANDLE * r);
void process_rdma_read_completion(vbuf* v);
void viadev_rget_start(MPIR_RHANDLE * r);

void viadev_incoming_eager_start(vbuf * v, viadev_connection_t * c,
                                 viadev_packet_eager_start * header);

void viadev_incoming_eager_coalesce(vbuf * v, viadev_connection_t * c,
        viadev_packet_eager_coalesce * header);

void viadev_incoming_eager_next(vbuf * v, viadev_connection_t * c,
                                viadev_packet_eager_next * header);

void viadev_rendezvous_push(MPIR_SHANDLE * s);

void viadev_eager_pull(MPIR_RHANDLE * rhandle);

void viadev_eager_coalesce_pull(MPIR_RHANDLE * rhandle);

void eager_coalesce(viadev_connection_t * c, char * buf,
        int len, viadev_packet_envelope * envelope);
void prepare_coalesced_pkt(viadev_connection_t * c, vbuf *v);

void viadev_send_noop_ifneeded(viadev_connection_t * c);

void viadev_send_noop(viadev_connection_t * c);

char *viadev_packet_to_string(struct ibv_qp *vi, vbuf * v);

extern char nullsbuffer, nullrbuffer;
void viadev_register_recvbuf_if_possible(MPIR_RHANDLE * rhandle);

void viadev_unimplemented(char *s);

int vi_to_grank(struct ibv_qp *vi);
void my_memcopy(char *dest, char *src, int nbytes);

void collect_vbuf_for_recv(int, viadev_connection_t *c);
void viadev_post_recv_list(viadev_connection_t *c, int num);
void flush_all_messages(void);

#ifdef MEMORY_RELIABLE
int enqueue_crc_queue(vbuf *vbuf_addr, viadev_connection_t *c);
int dequeue_crc_queue(packet_sequence_t ack, viadev_connection_t *c);
void init_crc_queues(void);
vbuf* search_crc_queue(viadev_connection_t *c, packet_sequence_t id);
void viadev_process_ack(vbuf * v, viadev_connection_t * c, viadev_packet_crc_ack * header);
void remove_ofo_queue(vbuf *vbuf_addr, viadev_connection_t *c);
void init_ofo_queues(void);
void enqueue_ofo_queue(vbuf *vbuf_addr,
                viadev_connection_t *c);
int check_ofo_queues(void **vbuf_addr);
#endif

void viadev_process_r3_ack(vbuf *v, viadev_connection_t *c, 
        viadev_packet_r3_ack *h);

#ifdef ADAPTIVE_RDMA_FAST_PATH

void vbuf_fast_rdma_alloc (viadev_connection_t * c, int dir);
void vbuf_rdma_address_send (viadev_connection_t * c);

#endif

#ifdef MEMORY_RELIABLE
void gen_crc_table(void);
unsigned long update_crc(unsigned long, char*, int);
int MPID_VIA_ack_send(int dest_grank, packet_sequence_t ack, int ack_type);
#endif

int viadev_post_srq_buffers(int);
void async_thread(void *ctx);

struct ibv_mr* register_memory(void *buf, int len);
int deregister_memory(struct ibv_mr *);

struct ibv_cq *create_cq(struct ibv_comp_channel *ch);
struct ibv_srq *create_srq();

int power_two(int);
static inline int eager_coalesce_ok(viadev_connection_t * c, int len) {

    D_PRINT("threshold: %d, sq_size: %d, send_wqe: %d\n",
            viadev_coalesce_threshold, viadev_sq_size, c->send_wqes_avail);

    if(viadev_use_eager_coalesce && 
            (viadev_sq_size - c->send_wqes_avail) >= viadev_coalesce_threshold && 
            len <= viadev_coalesce_threshold_size &&
            len <= (VBUF_BUFFER_SIZE - sizeof(viadev_packet_eager_coalesce) - 
                sizeof(viadev_packet_eager_coalesce_full))) {
        return 1;
    }
    return 0;
}

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

#define PREPOST_VBUF_RECV(c)  {                                     \
    vbuf *v = get_vbuf();                                           \
        vbuf_init_recv(v, VBUF_BUFFER_SIZE);                        \
        viadev_post_recv(c, v);                                     \
        c->local_credit++;                                          \
        c->preposts++;                                              \
}

#ifdef MCST_SUPPORT

#define PREPOST_UD_VBUF_RECV() {                                    \
        vbuf *v = get_vbuf();                                       \
        vbuf_init_recv(v, VBUF_BUFFER_SIZE);                        \
        viadev_post_ud_recv(v);                                     \
}

#endif

/* 
 * this should be a real function. Need to think carefully about
 * what has to be done. 
 * note model does MPID_UnpackMessageComplete, etc.
 */


#include "dreg.h"
#include <unistd.h>

#define SEND_COMPLETE(s) {                                          \
    s->is_complete = 1;                                             \
    if (s->dreg_entry != NULL) {                                    \
        dreg_decr_refcount(s->dreg_entry);                          \
    }                                                               \
    if (s->finish != NULL) {                                        \
        s->finish(s);                                               \
    }                                                               \
    s->dreg_entry = NULL;                                           \
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
    if (r->dreg_entry != NULL) {                                    \
        dreg_decr_refcount(r->dreg_entry);                          \
    }                                                               \
    r->dreg_entry = NULL;                                           \
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

#ifdef XRC
#define XRC_FILL_SRQN(sr, dst_rank) do {                            \
    if (viadev_use_xrc) {                                           \
        sr.xrc_remote_srq_num = viadev.srqn[dst_rank];              \
    } \
} while (0);

#define XRC_FILL_SRQN_FIX_CONN(v, c) do {                           \
    if (viadev_use_xrc) {                                           \
        v->desc.u.sr.xrc_remote_srq_num = viadev.srqn[c->global_rank];\
        if (c->xrc_flags & XRC_FLAG_INDIRECT_CONN) {                \
            c = &viadev.connections[c->xrc_qp_dst];                 \
            v->grank = c->xrc_qp_dst;                               \
        }                                                           \
    }                                                               \
} while (0);

#else
#define XRC_FILL_SRQN(sr, dst_rank) 
#define XRC_FILL_SRQN_FIX_CONN(v, c) 
#endif

#define IBV_POST_SR(v, c, err_string) {                             \
    {                                                               \
        struct ibv_wr *bad_wr;                                      \
        XRC_FILL_SRQN(v->desc.u.sr, c->global_rank);                \
        if(ibv_post_send(c->vi, &(v->desc.u.sr), &bad_wr)) {        \
            error_abort_all(IBV_RETURN_ERR, err_string);            \
        }                                                           \
    }                                                               \
}

#define RDMA_READ_START(c, r) {                                     \
    r->rdma_read_complete = 0;                                      \
    if(NULL == c->rdma_read_head) {                                 \
        c->rdma_read_head = r;                                      \
    } else {                                                        \
        c->rdma_read_tail->nexthandle = r;                          \
    }                                                               \
    c->rdma_read_tail = r;                                          \
}

#define VIADEV_EAGER_OK(_len, _c)                                   \
    (VIADEV_LIKELY(viadev_use_srq) ?                                \
        (VIADEV_LIKELY((_c)->ext_sendq_size <=                      \
                       viadev_eager_ok_threshold) ? 1 : 0) :        \
            viadev_eager_ok(_len, _c))


#endif                          /* _VIAPRIV_H */

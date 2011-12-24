
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

#ifdef _SMP_

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include "mpid.h"
#include "mpid_bind.h"
#include "mpid_smpi.h"
#include "psmpriv.h"
#include "queue.h"
//#include "mpiddev.h"
#include "reqalloc.h"

/* The memory barriers for PPC and MAC are same, since they are same
 * architecture */
#ifdef _PPC64_
#ifdef __GNUC__
/* can't use -ansi for vxworks ccppc or this will fail with a syntax error
 *  * */
#define STBAR()  asm volatile ("sync": : :"memory")     /* ": : :" for C++ */
#define READBAR() asm volatile ("sync": : :"memory")
#define WRITEBAR() asm volatile ("eieio": : :"memory")
#else
#if  defined (__IBMC__) || defined (__IBMCPP__)
extern void __iospace_eieio (void);
extern void __iospace_sync (void);
#define STBAR()   __iospace_sync ()
#define READBAR() __iospace_sync ()
#define WRITEBAR() __iospace_eieio ()
#else /*  defined (__IBMC__) || defined (__IBMCPP__) */
#error Do not know how to make a store barrier for this system
#endif /*  defined (__IBMC__) || defined (__IBMCPP__) */
#endif

#ifndef WRITEBAR
#define WRITEBAR() STBAR()
#endif
#ifndef READBAR
#define READBAR() STBAR()
#endif

#else
#define WRITEBAR()
#define READBAR()
#endif
extern int enable_shmem_collectives;
int MPID_SHMEM_COLL_init(void);
int MPID_SHMEM_COLL_Mmap(void);
int MPID_SHMEM_COLL_finalize(void);
void MPID_SHMEM_COLL_Unlink(void);

struct smpi_var smpi;
struct shared_mem *smpi_shmem;
struct shared_buffer_pool sh_buf_pool;
SEND_BUF_T **buffer_head;
SEND_BUF_T *my_buffer_head;
int MPID_n_pending = 0;
int SMP_INIT = 0;

#define MPID_PROGRESSION_LOCK()
#define MPID_PROGRESSION_UNLOCK()

unsigned int g_shmem_size = 0;
unsigned int g_shmem_size_pool = 0;
int smp_eagersize = SMP_EAGERSIZE;
int smpi_length_queue = SMPI_LENGTH_QUEUE;
int expect_cancel_ack = 0;

extern int smallmessages, largemessages, midmessages;
extern int smp_num_send_buffer;
extern int smp_batch_size;

#define HOSTNAME_LEN                          (255)
int smp_num_send_buffer = SMP_NUM_SEND_BUFFER;

static inline SEND_BUF_T *get_buf_from_pool (void);
static inline void send_buf_reclaim (void);
static inline void put_buf_to_pool (int, int);
static inline void link_buf_to_send_queue (int, int);

static inline void smpi_queue_send (void *, MPIR_SHANDLE *, int, int, void *);
static inline void smpi_post_send_queued (void *, void *, MPIR_SHANDLE *, int,
                                          int);
static inline void smpi_post_send_bufferinplace (void *, int, int, int, int,
                                                 int, MPIR_SHANDLE *);
static inline void smpi_complete_send (unsigned int, unsigned int,
                                       unsigned int);
static inline void smpi_complete_recv (unsigned int, unsigned int,
                                       unsigned int);
static inline unsigned int smpi_able_to_send (int, int);

static inline int MPID_SMP_Eagerb_recv_short (MPIR_RHANDLE *, int, void *);
static inline int MPID_SMP_Eagerb_save_short (MPIR_RHANDLE *, int, void *);
static inline void smpi_malloc_assert (void *, char *, char *);

static inline void smpi_post_send_rndv (void *, int, int, int, int, int, int,
                                        MPIR_SHANDLE *);
static inline void smpi_post_send_ok_to_send (int, MPIR_RHANDLE *);
static inline void smpi_post_send_ok_to_send_cont (int, MPIR_RHANDLE *);
static inline int smpi_recv_ok_to_send (int, int, void *, void *);
static inline int smpi_recv_get (int, int, void *);
static inline void smpi_send_cancelok (void *, int);
static inline void smpi_recv_cancelok (void *, int);

int smpi_sync_send = 0;

#ifdef RAHUL
/* send the SEND_request packet for rendez-vous */
static inline void
smpi_post_send_rndv (void *buf, int index, int len, int src_lrank, int tag,
                     int context_id, int destination, MPIR_SHANDLE * shandle)
{
  volatile void *ptr_volatile;
    void *ptr;
    SMPI_PKT_RNDV_T *pkt;
    int my_id = smpi.my_local_id;
    int dest = smpi.l2g_rank[destination];

    assert (destination > -1);
    assert (destination < smpi.num_local_nodes);

    ptr_volatile = (void *) ((smpi_shmem->pool)
                             + SMPI_NEXT (my_id, destination));
    ptr = (void *) ptr_volatile;
    pkt = (SMPI_PKT_RNDV_T *) ptr;
    pkt->mode = MPID_PKT_REQUEST_SEND;
    pkt->context_id = context_id;
    pkt->lrank = src_lrank;
    pkt->tag = tag;
    pkt->len = len;
    pkt->send_id = shandle;

    pkt->sequence_id = psmdev.connections[dest].next_packet_tosend;
    psmdev.connections[dest].next_packet_tosend++;

    pkt->address = index;

    smpi_complete_send (my_id, destination, sizeof (SMPI_PKT_RNDV_T));
}

/* send the OK_TO_SEND message to ack the SEND_REQUEST rendez-vous pkt */
static inline void
smpi_post_send_ok_to_send (int destination, MPIR_RHANDLE * rhandle)
{
    volatile void *ptr_volatile;
    void *ptr = NULL;
    SMPI_PKT_RNDV_T *pkt;
    int my_id = smpi.my_local_id;
    int dest = smpi.l2g_rank[destination];

    int msglen, last_pkt = 0;
    int index = rhandle->smp.index;
    SEND_BUF_T *recv_buf = NULL;

    assert (destination > -1);
    assert (destination < smpi.num_local_nodes);

    rhandle->smp.netbuf = rhandle->buf;
    rhandle->smp.current_offset = rhandle->len; 
    rhandle->s.MPI_ERROR = 0;
    rhandle->from = destination;

    while(index != -1){
        recv_buf = &buffer_head[destination][index];
        assert(recv_buf->busy == 1);

        msglen = recv_buf->len;
        if(msglen > 0){
            memcpy (rhandle->smp.netbuf, recv_buf->buf, msglen);
            rhandle->smp.netbuf += msglen;
            rhandle->smp.current_offset -= msglen;
        }    
        
        if(recv_buf->msg_complete ==1){
            recv_buf->busy = 0;
            last_pkt = 1;
            break;
        }

        if(recv_buf->has_next == 0){
            recv_buf->busy = 0;
            break;
        }

        index = recv_buf->next;
        recv_buf->busy = 0;
    }

    assert (rhandle->smp.current_offset >= 0);
    if (last_pkt) {
         rhandle->is_complete = 1;
         if (rhandle->finish)
             (rhandle->finish) (rhandle);
         return;
    }

    if (!smpi.send_fifo_head
        && smpi_able_to_send (destination, sizeof (SMPI_PKT_RNDV_T))) {
        /* build packet */
        ptr_volatile = (void *) ((smpi_shmem->pool)
                                 + SMPI_NEXT (my_id, destination));
        ptr = (void *) ptr_volatile;
        pkt = (SMPI_PKT_RNDV_T *) ptr;
        pkt->mode = MPID_PKT_OK_TO_SEND;
        /* should not matter */
        pkt->len = rhandle->smp.current_offset;
        pkt->send_id = rhandle->send_id;
        pkt->recv_id = rhandle;
        pkt->sequence_id = psmdev.connections[dest].next_packet_tosend;
        psmdev.connections[dest].next_packet_tosend++;

        /* flow control */
        smpi_complete_send (my_id, destination, sizeof (SMPI_PKT_RNDV_T));
    }
    else {
        /* not enough place, we will send it later */
        SMPI_PKT_RNDV_T *pkt_p;

        pkt_p = (SMPI_PKT_RNDV_T *) malloc (sizeof (SMPI_PKT_RNDV_T));
        smpi_malloc_assert (pkt_p,
                            "smpi_post_send_ok_to_send", "SMPI_PKT_RNDV_T");

        pkt_p->mode = MPID_PKT_OK_TO_SEND;
        pkt_p->len = rhandle->smp.current_offset;
        pkt_p->send_id = rhandle->send_id;
        pkt_p->recv_id = rhandle;
        pkt_p->sequence_id = psmdev.connections[dest].next_packet_tosend;
        psmdev.connections[dest].next_packet_tosend++;

        smpi_queue_send (pkt_p, 0, sizeof (SMPI_PKT_RNDV_T),
                         destination, NULL);
    }
}

/* send the OK_TO_SEND message to ack the SEND_REQUEST rendez-vous pkt */
static inline void
smpi_post_send_ok_to_send_cont (int destination, MPIR_RHANDLE * rhandle)
{
    volatile void *ptr_volatile;
    void *ptr;
    SMPI_PKT_RNDV_T *pkt;
    int my_id = smpi.my_local_id;
    int dest = smpi.l2g_rank[destination];

    assert (destination > -1);
    assert (destination < smpi.num_local_nodes);

    if (!smpi.send_fifo_head
        && smpi_able_to_send (destination, sizeof (SMPI_PKT_RNDV_T))) {
        /* build packet */
        ptr_volatile = (void *) ((smpi_shmem->pool)
                                 + SMPI_NEXT (my_id, destination));
        ptr = (void *) ptr_volatile;
        pkt = (SMPI_PKT_RNDV_T *) ptr;
        pkt->mode = MPID_PKT_OK_TO_SEND;
        pkt->len = rhandle->smp.current_offset;
        pkt->send_id = rhandle->send_id;
        pkt->recv_id = rhandle;
        pkt->sequence_id = psmdev.connections[dest].next_packet_tosend;
        psmdev.connections[dest].next_packet_tosend++;

        /* flow control */
        smpi_complete_send (my_id, destination, sizeof (SMPI_PKT_RNDV_T));
    }
    else {
        /* not enough place, we will send it later */
        SMPI_PKT_RNDV_T *pkt_p;
        pkt_p = (SMPI_PKT_RNDV_T *) malloc (sizeof (SMPI_PKT_RNDV_T));
        smpi_malloc_assert (pkt_p,
                            "smpi_post_send_ok_to_send_cont",
                            "SMPI_PKT_RNDV_T");

        pkt_p->mode = MPID_PKT_OK_TO_SEND;
        pkt_p->len = rhandle->smp.current_offset;
        pkt_p->send_id = rhandle->send_id;
        pkt_p->recv_id = rhandle;
        pkt_p->sequence_id = psmdev.connections[dest].next_packet_tosend;
        psmdev.connections[dest].next_packet_tosend++;

        smpi_queue_send (pkt_p, 0, sizeof (SMPI_PKT_RNDV_T),
                         destination, NULL);
    }
}

/* on the sender, when it receives the DONE_GET message, can complete 
   the rendez-vous Send and free the user buffer */
static inline int
smpi_recv_done_get (int from, int my_id, void *send_id)
{
    return 0;
}


static inline int
smpi_recv_ok_to_send (int from, int my_id, void *send_id, void *recv_id)
{
  volatile void *ptr_volatile;
  void *ptr;
  SMPI_PKT_CONT_GET_T *pkt;
  int destination = from;
  MPIR_SHANDLE *shandle;
  void *recv_handle_id, *send_handle_id;
  int length_to_send;
  int dest = smpi.l2g_rank[from];
  
  SEND_BUF_T *send_buf;
  SEND_BUF_T *tmp_buf;
  int index;
  int pktlen;
  short count;
  
  shandle = (MPIR_SHANDLE *) send_id;
  send_handle_id = send_id;
  recv_handle_id = recv_id;
  
  /* flow control */
  smpi_complete_recv (from, my_id, sizeof (SMPI_PKT_RNDV_T));
  
  assert (destination > -1);
  assert (destination < smpi.num_local_nodes);
  
  shandle->smp.current_expected--;
  assert (shandle->smp.current_expected >= 0);
  length_to_send = shandle->smp.current_offset;
  
  int flow_control_ok = 1;
  while ((flow_control_ok) && ((length_to_send > 0)
                                || (shandle->smp.current_done == 0))) {
    count = 0;
    index = -1;
    send_buf = NULL;
    tmp_buf = NULL; 
    
    send_buf_reclaim();
    
    while (length_to_send > 0) {
      if(count >= smp_batch_size) break;
      
      count++; 
      
      send_buf = get_buf_from_pool();
      if(send_buf == NULL){
        flow_control_ok = 0;
	break;
      }
      
      if(index == -1)
	index = send_buf->myindex;
      
      pktlen = (length_to_send > SMP_SEND_BUF_SIZE) ? SMP_SEND_BUF_SIZE : length_to_send;
      length_to_send -= pktlen;
      memcpy (send_buf->buf, shandle->smp.ptr, pktlen);
      send_buf->busy = 1;
      send_buf->len = pktlen;
      send_buf->has_next = 1;
      send_buf->msg_complete = (length_to_send == 0) ? 1 : 0;
      
      link_buf_to_send_queue (destination, send_buf->myindex);
      shandle->smp.ptr = (void *) ((unsigned long) shandle->smp.ptr + pktlen);
      tmp_buf = send_buf;
    }
    
    if(tmp_buf != NULL){
      tmp_buf->has_next = 0;
      assert(tmp_buf->has_next == 0 || tmp_buf->msg_complete == 0);
    }
    
    /* flow control */
    if (!smpi.send_fifo_head && smpi_able_to_send (destination, sizeof(SMPI_PKT_CONT_GET_T))) {
      /* build the packet */
      ptr_volatile = (void *) ((smpi_shmem->pool)
			       + SMPI_NEXT (my_id, destination));
      ptr = (void *) ptr_volatile;
      pkt = (SMPI_PKT_CONT_GET_T *) ptr;
      
      if (length_to_send == 0)
          pkt->mode = MPID_PKT_DONE_GET;
      else if (flow_control_ok == 0)
	  pkt->mode = MPID_PKT_FLOW;
      else
	  pkt->mode = MPID_PKT_CONT_GET;
      
      pkt->send_id = send_handle_id;
      pkt->recv_id = recv_handle_id;
      pkt->sequence_id = psmdev.connections[dest].next_packet_tosend;
      psmdev.connections[dest].next_packet_tosend++;

      pkt->len = 0;
      pkt->address = index;
      
      shandle->smp.current_done = 1;
      
      /* update flow control */
      smpi_complete_send (my_id, destination, sizeof(SMPI_PKT_CONT_GET_T));
    } 
    else {
      /* we need to keep the circuit full */
      SMPI_PKT_CONT_GET_T *pkt_p;
      pkt_p = (SMPI_PKT_CONT_GET_T *)
	malloc (sizeof (SMPI_PKT_CONT_GET_T));
      smpi_malloc_assert (pkt_p,
			  "smpi_recv_ok_to_send",
			  "SMPI_PKT_CONT_GET_T");

      if (length_to_send == 0){
          pkt_p->mode = MPID_PKT_DONE_GET; 
          shandle->smp.current_done = 1;
      }
      else
          pkt_p->mode = MPID_PKT_FLOW;

      flow_control_ok =0;

      pkt_p->send_id = send_handle_id;
      pkt_p->recv_id = recv_handle_id;
      pkt_p->len = 0;
      pkt_p->address = index;
      pkt_p->sequence_id =
	psmdev.connections[dest].next_packet_tosend;
      psmdev.connections[dest].next_packet_tosend++;
      
      smpi_queue_send (pkt_p, 0,
		       sizeof (SMPI_PKT_CONT_GET_T),
		       destination, NULL);
    }
  }
  
  shandle->smp.current_offset = length_to_send;
  
  if ((length_to_send == 0) && (shandle->smp.current_done == 1) && (shandle->smp.current_expected == 0)) {
    MPID_n_pending--;
    smpi.pending--;
    shandle->is_complete = 1;
    if (shandle->finish)
      shandle->finish (shandle);
  } else {
    shandle->smp.current_expected++;
  }
  return MPI_SUCCESS;
}

static inline int
smpi_recv_get (int from, int my_id, void *in_pkt)
{
  MPIR_RHANDLE *rhandle;
  SMPI_PKT_CONT_GET_T *pkt = (SMPI_PKT_CONT_GET_T *) in_pkt;
  SEND_BUF_T *recv_buf;
  int msglen, pkt_len, last_pkt = 0;
  int destination = from, index = pkt->address;
  
  rhandle = (MPIR_RHANDLE *) pkt->recv_id;
  pkt_len = sizeof(SMPI_PKT_CONT_GET_T);
  
  if (pkt->mode == MPID_PKT_DONE_GET)
    last_pkt = 1;
  
  while(index != -1){
    recv_buf = &buffer_head[destination][index];
    assert(recv_buf->busy == 1);
    
    msglen = recv_buf->len;
    if(msglen > 0){
      memcpy (rhandle->smp.netbuf, recv_buf->buf, msglen);
      rhandle->smp.netbuf += msglen;
      rhandle->smp.current_offset -= msglen;
    }
    
    if(recv_buf->msg_complete ==1){
      recv_buf->busy = 0;
      last_pkt = 1;
      break;
    }
    
    if(recv_buf->has_next == 0){
      recv_buf->busy = 0;
      break;
    }
    
    index = recv_buf->next;
    recv_buf->busy = 0;
  }
  
  /* flow control */
  smpi_complete_recv (from, my_id, pkt_len);
  
  if (last_pkt == 0 && pkt->mode == MPID_PKT_FLOW)
    smpi_post_send_ok_to_send_cont (from, rhandle);
  
  assert (rhandle->smp.current_offset >= 0);
  if (last_pkt) {
    rhandle->is_complete = 1;
    if (rhandle->finish)
      (rhandle->finish) (rhandle);
  }
  return MPI_SUCCESS;
}

static inline void
smpi_send_cancelok (void *in_pkt, int from)
{
    volatile void *ptr_volatile;
    void *ptr;
    int queued;
    SMPI_PKT_ANTI_SEND_T *pkt = (SMPI_PKT_ANTI_SEND_T *) in_pkt;
    SMPI_PKT_ANTI_SEND_T *new_pkt;

    int error_code;
    int found = 0;
    int dest = smpi.l2g_rank[from];

    MPIR_SHANDLE *shandle = 0;
    MPIR_RHANDLE *rhandle;

    MPID_PROGRESSION_LOCK ();

    /* Flow control : is there enough place on the shared memory area */
    if (!smpi.send_fifo_head
        && smpi_able_to_send (from, sizeof (SMPI_PKT_ANTI_SEND_T))) {
        queued = 0;
        ptr_volatile = (void *) ((smpi_shmem->pool)
                                 + SMPI_NEXT (smpi.my_local_id, from));
        ptr = (void *) ptr_volatile;
        new_pkt = (SMPI_PKT_ANTI_SEND_T *) ptr;
    }
    else {
        /* not enough place, we will send it later */
        queued = 1;
        new_pkt = (SMPI_PKT_ANTI_SEND_T *)
            malloc (sizeof (SMPI_PKT_ANTI_SEND_T));
        smpi_malloc_assert (new_pkt, "smpi_send_cancelok",
                            "SMPI_PKT_ANTI_SEND_T");
    }

    shandle = pkt->send_id;

    /* Look for request, if found, delete it */
    error_code =
        MPID_Search_unexpected_for_request (shandle, &rhandle, &found);

    if ((error_code != MPI_SUCCESS) || (found == 0)) {
        new_pkt->cancel = 0;
    }
    else {
        if (rhandle->s.count < smp_eagersize) {
            /* begin if short/eager message */
            FREE (rhandle->buf);
            rhandle->buf = 0;
            MPID_RecvFree (rhandle);
        }
        else {                  /* begin if rndv message */
            MPID_RecvFree (rhandle);
        }
        new_pkt->cancel = 1;
    }

    new_pkt->mode = MPID_PKT_ANTI_SEND_OK;
    new_pkt->send_id = shandle;
    new_pkt->sequence_id = psmdev.connections[dest].next_packet_tosend;
    psmdev.connections[dest].next_packet_tosend++;

    if (queued == 0) {
        /* update flow control */
        smpi_complete_send (smpi.my_local_id, from,
                            sizeof (SMPI_PKT_ANTI_SEND_T));
    }
    else {
        smpi_queue_send (new_pkt, 0, sizeof (SMPI_PKT_ANTI_SEND_T),
                         from, NULL);
    }

    MPID_PROGRESSION_UNLOCK ();
}

static inline void
smpi_recv_cancelok (void *in_pkt, int from)
{

    SMPI_PKT_ANTI_SEND_T *pkt = (SMPI_PKT_ANTI_SEND_T *) in_pkt;
    MPIR_SHANDLE *shandle = (MPIR_SHANDLE *) pkt->send_id;

    if (pkt->cancel) {          /* begin if pkt->cancel */
        /* Mark the request as cancelled */
        shandle->s.MPI_TAG = MPIR_MSG_CANCELLED;
        /* Mark the request as complete */
        shandle->is_complete = 1;
        shandle->is_cancelled = 1;
        if (shandle->smp.rendez_vous == 1) {
            MPID_n_pending--;
            smpi.pending--;
        }
    }                           /* end if pkt->cancel */
    else {
        shandle->is_cancelled = 0;
    }
    shandle->cancel_complete = 1;

    expect_cancel_ack--;
}


/*
 * This is really the same as the blocking version, since the 
 * nonblocking operations occur only in the data transmission.
 */
int
MPID_SMP_Rndvn_isend (void *buf, int len, int src_lrank, int tag,
                      int context_id, int dest, MPID_Msgrep_t msgrep,
                      MPIR_SHANDLE * shandle)
{
    int destination = smpi.local_nodes[dest];
    int current_len = len;
    int index = -1;
    void *current_buf = buf;

    if (smpi_sync_send == 0)
        MPID_PROGRESSION_LOCK ();

    /* enough place to send the REQUEST_SEND request ? */
    if (!smpi.send_fifo_head
        && smpi_able_to_send (destination, sizeof (SMPI_PKT_RNDV_T))) {
        smpi_post_send_rndv (buf, index, len, src_lrank, tag, context_id,
                             destination, shandle);
    }
    else {
        /* not enough place, we will send it later */
        SMPI_PKT_RNDV_T *pkt_p;
        pkt_p = (SMPI_PKT_RNDV_T *) malloc (sizeof (SMPI_PKT_RNDV_T));
        smpi_malloc_assert (pkt_p, "MPID_SMP_Rndvn_isend", "SMPI_PKT_RNDV_T");

        assert (dest < MPID_MyWorldSize);
        assert (dest > -1);
        assert (destination > -1);
        assert (destination < smpi.num_local_nodes);

        pkt_p->mode = MPID_PKT_REQUEST_SEND;
        pkt_p->context_id = context_id;
        pkt_p->lrank = src_lrank;
        pkt_p->tag = tag;
        pkt_p->len = len;
        pkt_p->send_id = shandle;
        pkt_p->sequence_id = psmdev.connections[dest].next_packet_tosend;
        psmdev.connections[dest].next_packet_tosend++;
        pkt_p->address = index;

        smpi_queue_send (pkt_p, 0, sizeof (SMPI_PKT_RNDV_T),
                         destination, NULL);
    }

    shandle->smp.ptr = current_buf;
    shandle->smp.current_offset = current_len;
    shandle->smp.current_expected = 1;
    shandle->smp.current_done = 0;
    shandle->smp.rendez_vous = 1;
    shandle->is_complete = 0;

    MPID_n_pending++;
    smpi.pending++;

    if (smpi_sync_send == 0) {
        MPID_PROGRESSION_UNLOCK ();
    }

    return MPI_SUCCESS;
}

int
MPID_SMP_Rndvn_save (rhandle, from, in_pkt)
     MPIR_RHANDLE *rhandle;
     int from;
     void *in_pkt;
{
    int my_id = smpi.my_local_id;

    SMPI_PKT_RNDV_T *pkt = (SMPI_PKT_RNDV_T *) in_pkt;

    rhandle->s.MPI_TAG = pkt->tag;
    rhandle->s.MPI_SOURCE = pkt->lrank;
    rhandle->s.MPI_ERROR = 0;
    rhandle->s.count = pkt->len;
    rhandle->is_complete = 0;
    rhandle->from = from;
    rhandle->unex_buf = NULL;
    rhandle->protocol = VIADEV_PROTOCOL_SMP_RNDV;
    rhandle->connection = (void *) 1;
    rhandle->smp.index = pkt->address;

    smpi_complete_recv (from, my_id, (sizeof (SMPI_PKT_RNDV_T)));

    return MPI_SUCCESS;
}



/* 
 * This routine is called when it is time to receive an unexpected
 * message.  
 */
int
MPID_SMP_Rndvn_unxrecv_posted (rhandle, in_runex)
     MPIR_RHANDLE *rhandle;
     void *in_runex;
{
    MPIR_RHANDLE *runex = (MPIR_RHANDLE *) in_runex;
    int destination = runex->from;
    int msglen, err = 0;

    msglen = runex->s.count;
    MPID_CHK_MSGLEN (rhandle, msglen, err);
    rhandle->s.count = msglen;

    rhandle->send_id = runex->send_id;
    rhandle->s.MPI_TAG = runex->s.MPI_TAG;
    rhandle->s.MPI_SOURCE = runex->s.MPI_SOURCE;
    rhandle->s.MPI_ERROR = 0;
    rhandle->s.count = runex->s.count;
    rhandle->from = destination;
    rhandle->is_complete = 0;
    rhandle->smp.index = runex->smp.index; 

    smpi_post_send_ok_to_send (destination, rhandle);

    MPID_RecvFree (runex);

    return err;
}

#endif

static inline void
smpi_malloc_assert (void *ptr, char *fct, char *msg)
{
    if (NULL == ptr)
        error_abort_all (GEN_EXIT_ERR,
                         "Cannot Allocate Memory: [%d] in function %s, context: %s\n",
                         MPID_MyWorldRank, fct, msg);
}

/* the init of the SMP device */
void
smpi_init (void)
{
    unsigned int i, j, pool, pid, wait;
    int local_num, sh_size, pid_len, rq_len, param_len, limit_len;
    struct stat file_status, file_status_pool;
    char *shmem_file = NULL;
    char *pool_file = NULL;
    int pagesize = getpagesize ();
    struct shared_mem *shmem;
    SEND_BUF_T *send_buf = NULL;
#ifdef _X86_64_
    volatile char tmpchar;
#endif 

    /* Convert to bytes */
    smp_eagersize = smp_eagersize * 1024 + 1;
    smpi_length_queue = smpi_length_queue * 1024;
    if (smp_eagersize > smpi_length_queue / 2) {
        fprintf (stderr, "SMP_EAGERSIZE should not exceed half of "
                 "SMPI_LENGTH_QUEUE. Note that SMP_EAGERSIZE "
                 "and SMPI_LENGTH_QUEUE are set in KBytes.\n");
        error_abort_all (GEN_EXIT_ERR,
                         "[%d] smpi_init: SMP_EAGERSIZE and/or SMPI_LENGTH_QUEUE "
                         "not correct\n", MPID_MyWorldRank);
    }

    smpi.pending = 0;
    smpi.send_fifo_queued = 0;
    smpi.available_queue_length =
        (smpi_length_queue - smp_eagersize - sizeof (SMPI_PKT_CONT_GET_T));

    /* add pid for unique file name */
    shmem_file =
        (char *) malloc (sizeof (char) * (HOSTNAME_LEN + 26 + PID_CHAR_LEN));

    pool_file =
        (char *) malloc (sizeof (char) * (HOSTNAME_LEN + 26 + PID_CHAR_LEN));

    smpi_malloc_assert (shmem_file, "smpi_init", "shared memory file name");
    smpi_malloc_assert (pool_file, "smpi_init", "pool file name");

    /* unique shared file name */
    sprintf (shmem_file, "/dev/shm/ib_shmem-%d-%s-%d.tmp", psmdev.global_id,
             psmdev.my_name, getuid ());

    sprintf (pool_file, "/dev/shm/ib_pool-%d-%s-%d.tmp", psmdev.global_id,
             psmdev.my_name, getuid ());

    /* open the shared memory file */
    smpi.fd =
        open (shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    if (smpi.fd < 0) {
        /* Fallback */
        sprintf (shmem_file, "/tmp/ib_shmem-%d-%s-%d.tmp", psmdev.global_id,
             psmdev.my_name, getuid ());

        sprintf (pool_file, "/tmp/ib_pool-%d-%s-%d.tmp", psmdev.global_id,
             psmdev.my_name, getuid ());
        smpi.fd =
            open (shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        if (smpi.fd < 0) {
            perror ("open");
            error_abort_all (GEN_EXIT_ERR,
                         "[%d] smpi_init:error in opening "
                         "shared memory file <%s>: %d\n",
                         MPID_MyWorldRank, shmem_file, errno);
	}
    }

    smpi.fd_pool =
        open (pool_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    if (smpi.fd_pool < 0) {
        perror ("open");
        error_abort_all (GEN_EXIT_ERR,
                         "[%d] smpi_init:error in opening "
                         "shared memory pool file <%s>: %d\n",
                         MPID_MyWorldRank, pool_file, errno);
    }

    /* compute the size of this file */
    local_num = smpi.num_local_nodes * smpi.num_local_nodes;
    pid_len = smpi.num_local_nodes * sizeof(int);
    param_len = sizeof(smpi_params) * local_num;
    rq_len = sizeof(smpi_rqueues) * local_num;
    limit_len = sizeof(smpi_rq_limit) * local_num;
    sh_size = sizeof(struct shared_mem) + pid_len + param_len + rq_len +
              limit_len + SMPI_CACHE_LINE_SIZE * 4;

    g_shmem_size = (SMPI_CACHE_LINE_SIZE + sh_size + pagesize +
            (smpi.num_local_nodes * (smpi.num_local_nodes - 1) *
             (SMPI_ALIGN (smpi_length_queue + pagesize))));

    g_shmem_size_pool =
        SMPI_ALIGN (sizeof (SEND_BUF_T) * smp_num_send_buffer +
                    pagesize) * smpi.num_local_nodes + SMPI_CACHE_LINE_SIZE;

    /* initialization of the shared memory file */
    /* just set size, don't really allocate memory, to allow intelligent memory allocation on NUMA arch */
    if (smpi.my_local_id == 0) {
        if (ftruncate (smpi.fd, 0)) {
            /* to clean up tmp shared file */
            unlink (shmem_file);
            error_abort_all (GEN_EXIT_ERR,
                             "[%d] smpi_init:error in ftruncate to zero "
                             "shared memory file: %d\n",
                             MPID_MyWorldRank, errno);
        }

        /* set file size, without touching pages */
        if (ftruncate (smpi.fd, g_shmem_size)) {
            /* to clean up tmp shared file */
            unlink (shmem_file);
            error_abort_all (GEN_EXIT_ERR,
                             "[%d] smpi_init:error in ftruncate to size "
                             "shared memory file: %d\n",
                             MPID_MyWorldRank, errno);
        }

        if (ftruncate (smpi.fd_pool, 0)) {
            /* to clean up tmp shared file */
            unlink (pool_file);
            error_abort_all (GEN_EXIT_ERR,
                             "[%d] smpi_init:error in ftruncate to zero "
                             "shared pool file: %d\n",
                             MPID_MyWorldRank, errno);
        }


        if (ftruncate (smpi.fd_pool, g_shmem_size_pool)) {
            /* to clean up tmp shared file */
            unlink (pool_file);
            error_abort_all (GEN_EXIT_ERR,
                             "[%d] smpi_init:error in ftruncate to size "
                             "shared pool file: %d\n",
                             MPID_MyWorldRank, errno);
        }

#ifndef _X86_64_
        {
            char *buf;
            buf = (char *) calloc (g_shmem_size + 1, sizeof (char));
            if (write (smpi.fd, buf, g_shmem_size) != g_shmem_size) {
                error_abort_all (GEN_EXIT_ERR,
                                 "[%d] smpi_init:error in writing "
                                 "shared memory file: %d\n",
                                 MPID_MyWorldRank, errno);
            }
            free (buf);
        }

        {
            char *buf;
            buf = (char *) calloc (g_shmem_size_pool + 1, sizeof (char));
            if (write (smpi.fd_pool, buf, g_shmem_size_pool) != g_shmem_size_pool) {
                error_abort_all (GEN_EXIT_ERR,
                                 "[%d] smpi_init:error in writing "
                                 "shared pool file: %d\n",
                                 MPID_MyWorldRank, errno);
            }
            free (buf);
        }

#endif
        if (lseek (smpi.fd, 0, SEEK_SET) != 0) {
            /* to clean up tmp shared file */
            unlink (shmem_file);
            error_abort_all (GEN_EXIT_ERR,
                             "[%d] smpi_init:error in lseek "
                             "on shared memory file: %d\n",
                             MPID_MyWorldRank, errno);
        }

        if (lseek (smpi.fd_pool, 0, SEEK_SET) != 0) {
            /* to clean up tmp shared file */
            unlink (pool_file);
            error_abort_all (GEN_EXIT_ERR,
                             "[%d] smpi_init:error in lseek "
                             "on shared pool file: %d\n",
                             MPID_MyWorldRank, errno);
        }

    }

    if (enable_shmem_collectives){
        /* Shared memory for collectives */
        MPID_SHMEM_COLL_init();
    }

    /* synchronization between local processes */
    do {
        if (fstat (smpi.fd, &file_status) != 0 ||
            fstat (smpi.fd_pool, &file_status_pool) != 0) {
            /* to clean up tmp shared file */
            unlink (shmem_file);
            error_abort_all (GEN_EXIT_ERR,
                             "[%d] smpi_init:error in fstat "
                             "on shared memory file: %d\n",
                             MPID_MyWorldRank, errno);
        }
        usleep (10);
    }
    while (file_status.st_size != g_shmem_size ||
           file_status_pool.st_size != g_shmem_size_pool);

    smpi_shmem = (struct shared_mem *)malloc(sizeof(struct shared_mem));
    smpi_malloc_assert(smpi_shmem, "smpi_init", "SMPI_SHMEM");

    /* mmap of the shared memory file */
    smpi.mmap_ptr = mmap (0, g_shmem_size,
                          (PROT_READ | PROT_WRITE), (MAP_SHARED), smpi.fd, 0);
    if (smpi.mmap_ptr == (void *) -1) {
        /* to clean up tmp shared file */
        unlink (shmem_file);
        error_abort_all (GEN_EXIT_ERR,
                         "[%d] smpi_init:error in mmapping "
                         "shared memory: %d\n", MPID_MyWorldRank, errno);
    }

    smpi.send_buf_pool_ptr = mmap (0, g_shmem_size_pool, (PROT_READ | PROT_WRITE),
                                   (MAP_SHARED), smpi.fd_pool, 0);

    if (smpi.send_buf_pool_ptr == (void *) -1) {
        unlink (pool_file);
        error_abort_all (GEN_EXIT_ERR,
                         "[%d] smpi_init:error in mmapping "
                         "shared pool memory: %d\n", MPID_MyWorldRank, errno);
    }

    shmem = (struct shared_mem *) smpi.mmap_ptr;
    if (((long) shmem & (SMPI_CACHE_LINE_SIZE - 1)) != 0) {
        /* to clean up tmp shared file */
        unlink (shmem_file);
        error_abort_all (GEN_EXIT_ERR,
                         "[%d] smpi_init:error in shifting mmapped "
                         "shared memory\n", MPID_MyWorldRank);
    }

    buffer_head = (SEND_BUF_T **) malloc(sizeof(SEND_BUF_T *) * smpi.num_local_nodes);
    for(i=0; i<smpi.num_local_nodes; i++){
        buffer_head[i] = (SEND_BUF_T *)((unsigned long)smpi.send_buf_pool_ptr +
                         SMPI_ALIGN(sizeof(SEND_BUF_T) * smp_num_send_buffer +
                         pagesize) * i);
        
        if (((long) buffer_head[i] & (SMPI_CACHE_LINE_SIZE - 1)) != 0) {
            unlink (pool_file);
            error_abort_all (GEN_EXIT_ERR,
                             "[%d] smpi_init:error in shifting mmapped "
                             "shared pool memory\n", MPID_MyWorldRank);
        } 
    }
    my_buffer_head = buffer_head[smpi.my_local_id];
    sh_buf_pool.free_head = 0;

    sh_buf_pool.send_queue = (int *)malloc(sizeof(int) * smpi.num_local_nodes);
    smpi_malloc_assert(sh_buf_pool.send_queue, "smpi_init", "sh_buf_pool.send_queue");

    sh_buf_pool.tail = (int *)malloc(sizeof(int) * smpi.num_local_nodes);
    smpi_malloc_assert(sh_buf_pool.tail, "smpi_init", "tail");

    for (i = 0; i < smpi.num_local_nodes; i++) {
        sh_buf_pool.send_queue[i] = sh_buf_pool.tail[i] = -1;
    }

#ifdef _X86_64_
    usleep(smpi.my_local_id);

    for (i = 0; i < smp_num_send_buffer; i++) {
        send_buf = &(my_buffer_head[i]);
        send_buf->myindex = i;
        send_buf->next = i+1;
        send_buf->busy = 0;
        send_buf->len = 0;
        send_buf->has_next = 0;
        send_buf->msg_complete = 0;

        for (j = 0; j < SMP_SEND_BUF_SIZE; j += pagesize) {
            tmpchar = (send_buf->buf)[j];
        }
    }
    if(send_buf) 
        send_buf->next = -1;

#else

    if (0 == smpi.my_local_id) {
        for(j = 0; j < smpi.num_local_nodes; j++){
            for (i = 0; i < smp_num_send_buffer; i++) {
                send_buf = &buffer_head[j][i];
                send_buf->myindex = i;
                send_buf->next = i+1;
                send_buf->busy = 0;
                send_buf->len = 0;
                send_buf->has_next = 0;
                send_buf->msg_complete = 0;
            }
            if(send_buf)
                send_buf->next = -1;
        }
    }
#endif

    /* Initialize shared_mem pointers */
    smpi_shmem->pid = (int *) shmem;

    smpi_shmem->rqueues_params =
         (smpi_params **) malloc(sizeof(smpi_params *)*smpi.num_local_nodes);
    smpi_shmem->rqueues_flow_out =
         (smpi_rqueues **) malloc(sizeof(smpi_rqueues *)*smpi.num_local_nodes);
    smpi_shmem->rqueues_limits =
        (smpi_rq_limit **)malloc(sizeof(smpi_rq_limit *)*smpi.num_local_nodes);

    if (smpi_shmem->rqueues_params == NULL ||
        smpi_shmem->rqueues_flow_out == NULL ||
        smpi_shmem->rqueues_limits == NULL) {
        error_abort_all (GEN_EXIT_ERR,
                             "Cannot Allocate Memory: [%d] in function "
                 "smpi_init, context: smpi_shmem rqueues\n",MPID_MyWorldRank);
    }
    smpi_shmem->rqueues_params[0] =
         (smpi_params *)((char *)shmem + SMPI_ALIGN(pid_len) + 
                        SMPI_CACHE_LINE_SIZE);
    smpi_shmem->rqueues_flow_out[0] =
         (smpi_rqueues *)((char *)smpi_shmem->rqueues_params[0] + 
                         SMPI_ALIGN(param_len) + SMPI_CACHE_LINE_SIZE);
    smpi_shmem->rqueues_limits[0] =
         (smpi_rq_limit *)((char *)smpi_shmem->rqueues_flow_out[0] + 
                          SMPI_ALIGN(rq_len) + SMPI_CACHE_LINE_SIZE);
    smpi_shmem->pool =
         (char *)((char *)smpi_shmem->rqueues_limits[0] + 
                 SMPI_ALIGN(limit_len) + SMPI_CACHE_LINE_SIZE);

    for (i = 1; i < smpi.num_local_nodes; i++) {
        smpi_shmem->rqueues_params[i] = (smpi_params *)
                    (smpi_shmem->rqueues_params[i-1] + smpi.num_local_nodes);
        smpi_shmem->rqueues_flow_out[i] = (smpi_rqueues *)
                    (smpi_shmem->rqueues_flow_out[i-1] + smpi.num_local_nodes);
        smpi_shmem->rqueues_limits[i] = (smpi_rq_limit *)
                    (smpi_shmem->rqueues_limits[i-1] + smpi.num_local_nodes);
    }

    /* init rqueues in shared memory */
    if (0 == smpi.my_local_id) {
        pool = pagesize;
        for (i = 0; i < smpi.num_local_nodes; i++) {
            for (j = 0; j < smpi.num_local_nodes; j++) {
                if (i != j) {
                    READBAR ();
                    smpi_shmem->rqueues_limits[i][j].first = 
                         SMPI_ALIGN (pool);
                    smpi_shmem->rqueues_limits[i][j].last =
                         SMPI_ALIGN (pool + smpi.available_queue_length);
                    smpi_shmem->rqueues_params[i][j].current = 
                         SMPI_ALIGN (pool);
                    smpi_shmem->rqueues_params[j][i].next =
                         SMPI_ALIGN(pool);
                    smpi_shmem->rqueues_params[j][i].msgs_total_in = 0;
                    smpi_shmem->rqueues_flow_out[i][j].msgs_total_out = 0;
                    READBAR ();
                    pool += SMPI_ALIGN (smpi_length_queue + pagesize);
                }
            }
        }
    }

    if (enable_shmem_collectives){
        /* Memory Mapping shared files for collectives*/
        MPID_SHMEM_COLL_Mmap();
    }


    /* another synchronization barrier */
    if (0 == smpi.my_local_id) {
        wait = 1;
        while (wait) {
            wait = 0;
            for (i = 1; i < smpi.num_local_nodes; i++) {
                if (smpi_shmem->pid[i] == 0) {
                    wait = 1;
                }
            }
        }
        /* id = 0, unlink the shared memory file, so that it is cleaned
         *       up when everyone exits */
        unlink (shmem_file);
        unlink (pool_file);

        if (enable_shmem_collectives){
            /* Unlinking shared files for collectives*/
            MPID_SHMEM_COLL_Unlink();
        }


        pid = getpid ();
        if (0 == pid) {
            error_abort_all (GEN_EXIT_ERR,
                             "[%d] smpi_init:error in geting pid\n",
                             MPID_MyWorldRank);
        }
        smpi_shmem->pid[smpi.my_local_id] = pid;
        WRITEBAR ();
    }
    else {
        while (smpi_shmem->pid[0] != 0);
        while (smpi_shmem->pid[0] == 0) {
            smpi_shmem->pid[smpi.my_local_id] = getpid ();
            WRITEBAR ();
        }
        for (i = 0; i < smpi.num_local_nodes; i++) {
            if (smpi_shmem->pid[i] <= 0) {
                error_abort_all (GEN_EXIT_ERR,
                                 "[%d] smpi_init:error in getting pid\n",
                                 MPID_MyWorldRank);
            }
        }
    }

    free (shmem_file);
    free (pool_file);

#ifdef _X86_64_
    /*
     * Okay, here we touch every page in the shared memory region.
     * We do this to get the pages allocated so that they are local
     * to the receiver on a numa machine (instead of all being located
     * near the first process).
     */
    {
        int receiver, sender;

        for (sender = 0; sender < smpi.num_local_nodes; sender++) {
            volatile char *ptr = smpi_shmem->pool;
            volatile char tmp;

            receiver = smpi.my_local_id;
            if (sender != receiver) {
                int k;

                for (k = SMPI_FIRST (sender, receiver);
                     k < SMPI_LAST (sender, receiver); k += pagesize) {
                    tmp = ptr[k];
                }
            }
        }
    }
#endif

    SMP_INIT = 1;
}

#ifdef RAHUL
/* Ok, we close everything and come back home */
void
smpi_finish (void)
{
    struct smpi_send_fifo_req *smpi_send_fifo_ptr = NULL;
    while (smpi.send_fifo_head || smpi.pending) {
        MPID_SMP_Check_incoming ();
    }
    /* unmap the shared memory file */
    munmap (smpi.mmap_ptr, g_shmem_size);
    close (smpi.fd);

    munmap(smpi.send_buf_pool_ptr, g_shmem_size_pool);
    close(smpi.fd_pool);

    smpi_send_fifo_ptr = smpi.send_fifo_head;
    while (smpi_send_fifo_ptr) {
        free (smpi_send_fifo_ptr);
        smpi_send_fifo_ptr = smpi_send_fifo_ptr->next;
    }
    if(smpi_shmem) {
        free(smpi_shmem);
    }
    if(buffer_head) {
        free(buffer_head);
    }

    if(sh_buf_pool.send_queue) {
        free(sh_buf_pool.send_queue);
    }

    if(sh_buf_pool.tail) {
        free(sh_buf_pool.tail);
    }

    if (enable_shmem_collectives){
        /* Freeing up shared memory collective resources*/
        MPID_SHMEM_COLL_finalize();
    }
    
}


static inline unsigned int
smpi_able_to_send (int dest, int len)
{
    return (((SMPI_TOTALIN (smpi.my_local_id, dest) >=
              SMPI_TOTALOUT (smpi.my_local_id, dest))
             && (SMPI_TOTALIN (smpi.my_local_id, dest)
                 - SMPI_TOTALOUT (smpi.my_local_id, dest) +
                 SMPI_ALIGN (len) < smpi.available_queue_length)) ||
            ((SMPI_TOTALIN (smpi.my_local_id, dest) <
              SMPI_TOTALOUT (smpi.my_local_id, dest)) &&
             (SMPI_MAX_INT - SMPI_TOTALOUT (smpi.my_local_id, dest) +
              SMPI_TOTALIN (smpi.my_local_id, dest) + SMPI_ALIGN (len) <
              smpi.available_queue_length)));
}

static inline void
smpi_complete_send (unsigned int my_id,
                    unsigned int destination, unsigned int length)
{

    SMPI_NEXT (my_id, destination) += SMPI_ALIGN (length);

    if (SMPI_NEXT (my_id, destination) > SMPI_LAST (my_id, destination)) {
        SMPI_NEXT (my_id, destination) = SMPI_FIRST (my_id, destination);
    }
    WRITEBAR ();
    SMPI_TOTALIN (my_id, destination) += SMPI_ALIGN (length);

}

static inline void
smpi_complete_recv (unsigned int from_grank,
                    unsigned int my_id, unsigned int length)
{
    SMPI_CURRENT (from_grank, my_id) += SMPI_ALIGN (length);

    if (SMPI_CURRENT (from_grank, my_id) > SMPI_LAST (from_grank, my_id)) {
        SMPI_CURRENT (from_grank, my_id) = SMPI_FIRST (from_grank, my_id);
    }
    WRITEBAR ();
    SMPI_TOTALOUT (from_grank, my_id) += SMPI_ALIGN (length);
}


static inline void
smpi_post_send_bufferinplace (void *buf,
                              int len,
                              int src_lrank,
                              int tag,
                              int context_id,
                              int destination, MPIR_SHANDLE * shandle)
{
    volatile void *ptr_volatile;
    void *ptr;
    SMPI_PKT_SHORT_T *pkt;
    unsigned int my_id = smpi.my_local_id;
    int dest = smpi.l2g_rank[destination];

    /* build the packet */
    ptr_volatile = (void *) ((smpi_shmem->pool)
                             + SMPI_NEXT (my_id, destination));
    ptr = (void *) ptr_volatile;
    pkt = (SMPI_PKT_SHORT_T *) ptr;
    pkt->mode = MPID_PKT_SHORT;
    pkt->context_id = context_id;
    pkt->lrank = src_lrank;
    pkt->tag = tag;
    pkt->len = len;
    pkt->send_id = shandle;

    pkt->sequence_id = psmdev.connections[dest].next_packet_tosend;
    psmdev.connections[dest].next_packet_tosend++;

    /*copy the data from user buffer */
    if (len > 0) {
        memcpy ((void *) ((unsigned long) ptr +
                          sizeof (SMPI_PKT_HEAD_T)), buf, len);
    }
    /* update flow control */
    smpi_complete_send (my_id, destination, (len + sizeof (SMPI_PKT_HEAD_T)));

}

int
MPID_SMP_Check_incoming (void)
{
    int found;
    int send = 0;
    struct smpi_send_fifo_req *sreq;
    while ((sreq = smpi.send_fifo_head) &&
           smpi_able_to_send (sreq->grank, sreq->len)) {
        smpi_post_send_queued (sreq->data, sreq->isend_data,
                               sreq->shandle, sreq->len, sreq->grank);
        send = 1;
        if (sreq == smpi.send_fifo_tail) {
            smpi.send_fifo_head = NULL;
            smpi.send_fifo_tail = NULL;
            if (smpi.send_fifo_queued != 1) {
                error_abort_all (GEN_EXIT_ERR,
                                 "error at smp_sheck_incoming\n");
            }
        }
        else {
            smpi.send_fifo_head = sreq->next;
            if (smpi.send_fifo_queued <= 0) {
                error_abort_all (GEN_EXIT_ERR,
                                 "error at smp_sheck_incoming\n");
            }
        }
        smpi.send_fifo_queued--;
        free (sreq->data);
        free (sreq);
    }
    /* polling */
    found = smpi_net_lookup ();

    if (send == 1) {
        found = MPI_SUCCESS;
    }
    return found;
}

int
smpi_net_lookup (void)
{
    volatile void *ptr;
    SMPI_PKT_T *pkt;
    int from, j;
    MPIR_RHANDLE *rhandle;
    int is_posted;
    int err = -1;
    int global_rank;

    for (j = 1; j < smpi.num_local_nodes; j++) {
        from = (smpi.my_local_id + j) % smpi.num_local_nodes;
        while (SMPI_TOTALIN (from, smpi.my_local_id) !=
               SMPI_TOTALOUT (from, smpi.my_local_id)) {
            global_rank = smpi.l2g_rank[from];
            READBAR ();
            ptr = (void *) ((smpi_shmem->pool) +
                            SMPI_CURRENT (from, smpi.my_local_id));
            pkt = (SMPI_PKT_T *) ptr;

            if (pkt->head.sequence_id !=
                psmdev.connections[global_rank].next_packet_expected) {
                break;
            }
            else {
                psmdev.connections[global_rank].next_packet_expected++;
            }
            /* Separate the incoming messages from control messages */
            if (MPID_PKT_IS_MSG (pkt->head.mode)) {
                MPID_Msg_arrived (pkt->head.lrank,
                                  pkt->head.tag,
                                  pkt->head.context_id, &rhandle, &is_posted);
                if (is_posted) {
                    switch (pkt->head.mode) {
                    case MPID_PKT_SHORT:
                        {
                            MPID_SMP_Eagerb_recv_short (rhandle, from, pkt);
                            err = MPI_SUCCESS;
                            break;
                        }
#ifdef _SMP_RNDV_
                    case MPID_PKT_REQUEST_SEND:
                        {
                            rhandle->s.MPI_TAG = pkt->rndv_pkt.tag;
                            rhandle->s.MPI_SOURCE = pkt->rndv_pkt.lrank;
                            rhandle->s.count = pkt->rndv_pkt.len;
                            rhandle->send_id = pkt->rndv_pkt.send_id;
                            /* Message was removed from posted list.
                             * From now we can not cancel it
                             */
                            rhandle->can_cancel = 0;
                            rhandle->smp.index = pkt->rndv_pkt.address;

                            /* flow control to receive the REQUEST_SEND */
                            smpi_complete_recv
                                (from,
                                 smpi.my_local_id, sizeof (SMPI_PKT_RNDV_T));

                            smpi_post_send_ok_to_send (from, rhandle);
                            err = MPI_SUCCESS;
                            break;
                        }
#endif
                    default:
                        D_PRINT ("[%d] Internal error: msg packet "
                                 "discarded (%s:%d)\n", MPID_MyWorldRank,
                                 __FILE__, __LINE__);
                    }
                }
                else {
                    switch (pkt->head.mode) {
                    case MPID_PKT_SHORT:
                        {
                            rhandle->send_id = 0;
                            if (MPID_SMP_Eagerb_save_short
                                (rhandle, from, pkt)) {
                                error_abort_all (GEN_EXIT_ERR,
                                                 "[%d] malloc failed\n",
                                                 MPID_MyWorldRank);
                            }
                            err = MPI_SUCCESS;
                            break;
                        }
#ifdef _SMP_RNDV_
                    case MPID_PKT_REQUEST_SEND:
                        {
                            /* Need the send handle address to cancel a send */
                            rhandle->send_id = pkt->rndv_pkt.send_id;
                            MPID_SMP_Rndvn_save (rhandle, from, pkt);
                            err = MPI_SUCCESS;
                            break;
                        }
#endif
                    default:
                        D_PRINT ("[%d] Internal error: msg packet "
                                 "discarded (%s:%d)\n", MPID_MyWorldRank,
                                 __FILE__, __LINE__);
                    }
                }
            }
#ifdef _SMP_RNDV_
            else {
                switch (pkt->head.mode) {
                case MPID_PKT_OK_TO_SEND:
                    {
                        smpi_recv_ok_to_send (from,
                                              smpi.
                                              my_local_id,
                                              pkt->
                                              rndv_pkt.
                                              send_id, pkt->rndv_pkt.recv_id);
                        err = MPI_SUCCESS;
                        break;
                    }

                case MPID_PKT_FLOW:
                case MPID_PKT_CONT_GET:
                case MPID_PKT_DONE_GET:
                    {
                        smpi_recv_get (from, smpi.my_local_id, pkt);
                        err = MPI_SUCCESS;
                        break;
                    }

                case MPID_PKT_ANTI_SEND:
                    {
                        smpi_send_cancelok (pkt, from);
                        /* flow control to receive the ANTI_SEND */
                        smpi_complete_recv (from,
                                            smpi.
                                            my_local_id,
                                            sizeof (SMPI_PKT_ANTI_SEND_T));

                        err = MPI_SUCCESS;
                        break;
                    }

                case MPID_PKT_ANTI_SEND_OK:
                    {
                        smpi_recv_cancelok (pkt, from);
                        /* flow control to receive the ANTI_SEND_OK */
                        smpi_complete_recv (from,
                                            smpi.
                                            my_local_id,
                                            sizeof (SMPI_PKT_ANTI_SEND_T));

                        err = MPI_SUCCESS;
                        break;
                    }

                default:
                    D_PRINT ("[%d] Internal error: msg packet "
                             "discarded (%s:%d)\n", MPID_MyWorldRank,
                             __FILE__, __LINE__);
                }
            }
#endif
        }
    }
    return err;

}

static inline void
smpi_post_send_queued (void *data, void *isend_data,
                       MPIR_SHANDLE * shandle, int len, int destination)
{
    volatile void *ptr_volatile;
    void *ptr;
    int my_id = smpi.my_local_id;

    ptr_volatile =
        (void *) ((smpi_shmem->pool) + SMPI_NEXT (my_id, destination));
    ptr = (void *) ptr_volatile;

    if (isend_data == NULL) {
        /* the packet is already built */
        if (len > 0) {
            memcpy (ptr, data, len);
        }
        smpi_complete_send (my_id, destination, len);
        if (shandle) {
            shandle->is_complete = 1;
        }
    }
    else {
        int datalen = len - sizeof (SMPI_PKT_HEAD_T);

        memcpy (ptr, data, sizeof (SMPI_PKT_HEAD_T));
        if (datalen > 0)
            memcpy ((void *) ((unsigned long) ptr +
                              sizeof (SMPI_PKT_HEAD_T)), isend_data, datalen);

        smpi_complete_send (my_id, destination, len);
        if (shandle) {
            shandle->is_complete = 1;
        }
    }
}


static inline int
MPID_SMP_Eagerb_recv_short (MPIR_RHANDLE * rhandle,
                            int from_lrank, void *in_pkt)
{
    SMPI_PKT_SHORT_T *pkt = (SMPI_PKT_SHORT_T *) in_pkt;
    int msglen;
    int err = MPI_SUCCESS;
    int my_id = smpi.my_local_id;
    msglen = pkt->len;
    rhandle->s.MPI_TAG = pkt->tag;
    rhandle->s.MPI_SOURCE = pkt->lrank;

    /* do not overflow the receive buffer if the message is too big */
    if (msglen > rhandle->len) {
        msglen = rhandle->len;
        err = MPI_ERR_TRUNCATE;
    }

    if (msglen > 0) {
        memcpy (rhandle->buf,
                (void *) ((unsigned long) pkt +
                          sizeof (SMPI_PKT_SHORT_T)), msglen);
    }

    rhandle->s.count = msglen;
    rhandle->s.MPI_ERROR = err;
    rhandle->is_complete = 1;
    rhandle->can_cancel = 0;
    smpi_complete_recv (from_lrank, my_id,
                        (pkt->len + sizeof (SMPI_PKT_HEAD_T)));

    if (rhandle->finish) {
        (rhandle->finish) (rhandle);
    }

    return err;
}


int
MPID_SMP_Eagerb_send_short (void *buf,
                            int len,
                            int src_lrank,
                            int tag,
                            int context_id,
                            int dest,
                            MPID_Msgrep_t msgrep, MPIR_SHANDLE * shandle)
{
    int destination = smpi.local_nodes[dest];
    /* Flow control : is there enough place on the shared memory area */
    if (!smpi.send_fifo_head &&
        smpi_able_to_send (destination, len + sizeof (SMPI_PKT_HEAD_T))) {
        /* let's go to send it ! */
        smpi_post_send_bufferinplace (buf,
                                      len, src_lrank, tag, context_id,
                                      destination, shandle);
        shandle->is_complete = 1;
    }
    else {
        SMPI_PKT_SHORT_T *pkt_ptr;
        pkt_ptr = (SMPI_PKT_SHORT_T *) malloc (len +
                                               sizeof (SMPI_PKT_HEAD_T));

        smpi_malloc_assert (pkt_ptr, "MPID_SMP_Eagerb_isend_short",
                            "SMPI_PKT_SHORT_T");

        /* These references are ordered to match the order they appear in the 
         *       structure */
        pkt_ptr->mode = MPID_PKT_SHORT;
        pkt_ptr->context_id = context_id;
        pkt_ptr->lrank = src_lrank;
        pkt_ptr->tag = tag;
        pkt_ptr->len = len;
        pkt_ptr->sequence_id = psmdev.connections[dest].next_packet_tosend;
        psmdev.connections[dest].next_packet_tosend++;

        if (!(dest < MPID_MyWorldSize) ||
            !(len < smp_eagersize) || !(destination > -1)
            || !(destination < smpi.num_local_nodes)) {
            error_abort_all (GEN_EXIT_ERR,
                             "Wrong in MPID_SMP_Eagerb_isend_short\n");
        }
        if (len > 0) {
            memcpy ((void *) ((unsigned long) pkt_ptr +
                              sizeof (SMPI_PKT_SHORT_T)), buf, len);
        }
        smpi_queue_send (pkt_ptr, shandle,
                         (len + sizeof (SMPI_PKT_HEAD_T)), destination, NULL);
        shandle->is_complete = 0;
    }
    return MPI_SUCCESS;
}

int
MPID_SMP_Eagerb_isend_short (void *buf,
                             int len,
                             int src_lrank,
                             int tag,
                             int context_id,
                             int dest,
                             MPID_Msgrep_t msgrep, MPIR_SHANDLE * shandle)
{
    int destination = smpi.local_nodes[dest];
    /* Flow control : is there enough place on the shared memory area */
    if (!smpi.send_fifo_head &&
        smpi_able_to_send (destination, len + sizeof (SMPI_PKT_HEAD_T))) {
        /* let's go to send it ! */
        smpi_post_send_bufferinplace (buf,
                                      len, src_lrank, tag, context_id,
                                      destination, shandle);
        shandle->is_complete = 1;
    }
    else {
        SMPI_PKT_SHORT_T *pkt_ptr;
        pkt_ptr = (SMPI_PKT_SHORT_T *)
            malloc (sizeof (SMPI_PKT_HEAD_T));

        smpi_malloc_assert (pkt_ptr, "MPID_SMP_Eagerb_isend_short",
                            "SMPI_PKT_SHORT_T");

        /* These references are ordered to match the order they appear in the 
         *       structure */
        pkt_ptr->mode = MPID_PKT_SHORT;
        pkt_ptr->context_id = context_id;
        pkt_ptr->lrank = src_lrank;
        pkt_ptr->tag = tag;
        pkt_ptr->len = len;
        pkt_ptr->sequence_id = psmdev.connections[dest].next_packet_tosend;
        psmdev.connections[dest].next_packet_tosend++;

        if (!(dest < MPID_MyWorldSize) ||
            !(len < smp_eagersize) || !(destination > -1)
            || !(destination < smpi.num_local_nodes)) {
            error_abort_all (GEN_EXIT_ERR,
                             "Wrong in MPID_SMP_Eagerb_isend_short\n");
        }
        /*
           if (len > 0) {
           memcpy((void *) ((unsigned long) pkt_ptr +
           sizeof(SMPI_PKT_SHORT_T)), buf, len);
           }
           smpi_queue_send(pkt_ptr, shandle, (len + sizeof(SMPI_PKT_HEAD_T)),
           destination); */

        smpi_queue_send (pkt_ptr, shandle,
                         (len + sizeof (SMPI_PKT_HEAD_T)), destination, buf);

        shandle->is_complete = 0;
    }
    return MPI_SUCCESS;
}

/* 
 * This routine is called when it is time to receive an unexpected
 * message
 */
int
MPID_SMP_Eagerb_unxrecv_start_short (MPIR_RHANDLE * rhandle,
                                     MPIR_RHANDLE * runex)
{
    int msglen, err = 0;
    msglen = runex->s.count;

    /* do not overflow the receive buffer if the message is too big */
    if (msglen > rhandle->len) {
        msglen = rhandle->len;
        err = MPI_ERR_TRUNCATE;
    }

    if (msglen > 0) {
        memcpy (rhandle->buf, runex->buf, msglen);
    }

    if (runex->s.count > 0) {
        FREE (runex->buf);
    }
    rhandle->s = runex->s;
    rhandle->s.count = msglen;
    rhandle->s.MPI_ERROR = err;
    rhandle->is_complete = 1;
    if (rhandle->finish)
        (rhandle->finish) (rhandle);
    MPID_RecvFree (runex);

    return err;
}

static inline int
MPID_SMP_Eagerb_save_short (MPIR_RHANDLE * rhandle, int from, void *in_pkt)
{
    int my_id = smpi.my_local_id;
    SMPI_PKT_SHORT_T *pkt = (SMPI_PKT_SHORT_T *) in_pkt;

    rhandle->protocol = VIADEV_PROTOCOL_SMP_SHORT;
    rhandle->connection = (void *) 1;
    rhandle->s.MPI_TAG = pkt->tag;
    rhandle->s.MPI_SOURCE = pkt->lrank;
    rhandle->s.MPI_ERROR = 0;
    rhandle->s.count = pkt->len;
    rhandle->send_id = pkt->send_id;
    if (pkt->len > 0) {
        rhandle->buf = (void *) MALLOC (pkt->len);
        if (!rhandle->buf) {
            rhandle->s.MPI_ERROR = MPI_ERR_INTERN;
            return 1;
        }
        memcpy (rhandle->buf, (void *) ((unsigned long) pkt +
                                        sizeof (SMPI_PKT_SHORT_T)), pkt->len);
    }
    smpi_complete_recv (from, my_id, (pkt->len + sizeof (SMPI_PKT_HEAD_T)));
    return 0;
}

/* add a send request in the snd_request fifo to send it later */
static inline void
smpi_queue_send (void *data,
                 MPIR_SHANDLE * shandle, int len, int grank, void *buf)
{
    struct smpi_send_fifo_req *smpi_send_fifo_ptr = NULL;
    smpi_send_fifo_ptr = (struct smpi_send_fifo_req *)
        malloc (sizeof (struct smpi_send_fifo_req));

    smpi_malloc_assert (smpi_send_fifo_ptr, "smpi_queue_send",
                        "smpi_send_fifo_req");

    smpi_send_fifo_ptr->data = data;
    smpi_send_fifo_ptr->isend_data = buf;
    smpi_send_fifo_ptr->shandle = shandle;
    smpi_send_fifo_ptr->len = len;
    smpi_send_fifo_ptr->grank = grank;
    smpi_send_fifo_ptr->next = NULL;

    if (NULL == smpi.send_fifo_head) {
        smpi.send_fifo_head = smpi_send_fifo_ptr;
    }
    else {
        smpi.send_fifo_tail->next = smpi_send_fifo_ptr;
    }

    smpi.send_fifo_tail = smpi_send_fifo_ptr;
    smpi.send_fifo_queued++;
}

/* --------------------------------------------- */
static inline SEND_BUF_T *
get_buf_from_pool ()
{
    SEND_BUF_T *ptr;

    if (sh_buf_pool.free_head == -1)
        return NULL;

    ptr = &(my_buffer_head[sh_buf_pool.free_head]);    
    sh_buf_pool.free_head = ptr->next;
    ptr->next = -1;

    assert (ptr->busy == 0);
    
    return ptr;
}

static inline void
send_buf_reclaim ()
{
    int i, index, last_index;
    SEND_BUF_T *ptr;

    for (i = 0; i < smpi.num_local_nodes; i++) {
        if (i != smpi.my_local_id) {
            index = sh_buf_pool.send_queue[i];
            last_index = -1;
            ptr = NULL;
            while (index != -1) {
                ptr = &(my_buffer_head[index]);
                if(ptr->busy == 1)
                     break;
                last_index = index;
                index = ptr->next;
            }
            if (last_index != -1)
                put_buf_to_pool (sh_buf_pool.send_queue[i], last_index);
            sh_buf_pool.send_queue[i] = index;
            if (sh_buf_pool.send_queue[i] == -1)
                sh_buf_pool.tail[i] = -1;
        }
    }
}

static inline void
put_buf_to_pool (int head, int tail)
{
  SEND_BUF_T *ptr;
  
  assert (head != -1);
  assert (tail != -1);
  
  ptr = &(my_buffer_head[tail]);
  
  ptr->next = sh_buf_pool.free_head;
  sh_buf_pool.free_head = head;
}

static inline void
link_buf_to_send_queue (int dest, int index)
{
    if (sh_buf_pool.send_queue[dest] == -1) {
        sh_buf_pool.send_queue[dest] = sh_buf_pool.tail[dest] = index;
        return;
    }

    my_buffer_head[(sh_buf_pool.tail[dest])].next = index;
    sh_buf_pool.tail[dest] = index;
}
#endif/*RAHUL*/
#endif /* _SMP_ */


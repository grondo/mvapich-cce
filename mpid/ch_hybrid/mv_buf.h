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


#ifndef _VBUF_H
#define _VBUF_H

#include <pthread.h>
#include "ibverbs_header.h"
#include "mv_param.h"
#include "get_clock.h"

#define MVBUF_HEAD_FLAG_TYPE uint32_t
#define MVBUF_TAIL_FLAG_TYPE uint32_t

#define MVBUF_FLAG_VAL 0
#define MVBUF_FLAG_VAL_ALT 1

#define FAST_RDMA_SIZE_MASK 0x7fff
#define FAST_RDMA_ALT_TAG 0x8000
#define MV_RCFP_OVERHEAD (sizeof(MVBUF_HEAD_FLAG_TYPE) + sizeof(MVBUF_TAIL_FLAG_TYPE))

typedef enum {
   MVDEV_REG_FLAG,
   MVDEV_RPUT_FLAG,
   MVDEV_RGET_FLAG,
   MVDEV_RC_FP,
} mv_flags;

typedef enum {
   MVDEV_RC_FP_FREE,
   MVDEV_RC_FP_INUSE,
} mv_buf_status;

typedef enum {
    THREAD_NO = 50,
    THREAD_OK = 51
} mvbuf_thread_status;

/* TODO: add sdescriptor region */

typedef struct mv_sdescriptor {
    struct ibv_send_wr sr;
    struct ibv_sge sg_entry;
    void * qp;
    void *parent;
    struct mv_sdescriptor * next_extsendq;
} mv_sdescriptor;

typedef struct mv_rdescriptor {
    struct ibv_recv_wr rr;
    struct ibv_sge sg_entry;
    void *parent;
    void * qp;
} mv_rdescriptor;

typedef struct listptr {
    void *next;
    void *prev;
} listptr;

typedef struct mv_buf {
    unsigned char *ptr; 
    struct mv_buf_region *region;
    struct mv_buf * next_ptr;
    unsigned int byte_len;
} mv_buf;

typedef struct mv_sdescriptor_size {
    int count;
    struct mv_sdescriptor_chunk * head;
} mv_sdescriptor_size;

typedef struct mv_sdescriptor_chunk {
    struct mv_sdescriptor * desc;
    struct mv_sdescriptor_size * size;
    struct mv_sdescriptor_chunk *next;
} mv_sdescriptor_chunk;

typedef struct mv_sdescriptor_region {
    unsigned char *desc_ptr;
    unsigned char *chunk_ptr;
    struct mv_sdescriptor_chunk * array;
    struct mv_sdescriptor_size * size;
    struct mv_sdescriptor_region *next;
} mv_sdescriptor_region;

typedef struct mv_sbuf {
    unsigned char *base_ptr;
    unsigned char *header_ptr;
    mv_buf * buf;
    mv_sdescriptor desc;
    mv_sdescriptor_chunk * desc_chunk;
    uint32_t max_data_size;

    listptr extwin_ptr;
    listptr sendwin_ptr;
    listptr unackq_ptr;
    struct mv_sbuf * next_ptr;
    struct mv_sbuf_region * region;
    uint8_t rel_type;

    uint32_t seqnum;
    uint32_t seqnum_last;

    uint32_t rank;
    uint32_t total_len;

    uint8_t left_to_send;
    uint8_t retry_always;
    uint8_t flag;
    uint8_t in_sendwin;
    uint8_t transport;
    uint8_t in_progress;
    uint16_t segments;
    uint16_t retry_count;

    void *shandle;
    double timestamp;

    uint8_t header;
} mv_sbuf;

typedef struct mv_rbuf {
    unsigned char *base_ptr;
    unsigned char *mheader_ptr;
    unsigned char *header_ptr;
    unsigned char *data_ptr;
    uint32_t max_data_size;
    mv_buf * buf;
    mv_rdescriptor desc;

    uint32_t byte_len;
    uint32_t byte_len_data;
    uint32_t byte_len_full;

    listptr recvwin_ptr;
    listptr eager_ptr;
    struct mv_rbuf * next_ptr;
    struct mv_rbuf_region * region;

    uint32_t seqnum;
    uint32_t rank;
    uint8_t has_header;
    uint8_t transport;
    void * rpool;

    uint8_t flag;
    uint8_t status;
    uint8_t header;
} mv_rbuf;


typedef struct mv_buf_size {
    uint32_t max_data_size;
    uint32_t alloc_size;
    struct mv_buf_region * region;
    struct mv_buf * head;
    listptr next;
} mv_buf_size;


typedef struct mv_buf_region {
    struct mv_buf_size *size_ptr;
    struct ibv_mr *mem_handle[MAX_NUM_HCAS];
    unsigned char *mv_buf_ptr;
    unsigned char *data_ptr;
    struct mv_buf_region *next;
    struct mv_buf * array;
    int count;
} mv_buf_region;

typedef struct mv_rbuf_region {
    unsigned char *mv_rbuf_ptr;
    struct mv_rbuf_region *next;
    struct mv_rbuf * array;
    int count;
} mv_rbuf_region;

typedef struct mv_sbuf_region {
    unsigned char *mv_sbuf_ptr;
    struct mv_sbuf_region *next;
    struct mv_sbuf * array;
    int count;
} mv_sbuf_region;

#define MV_HEADER_NORM_OFFSET_SIZE (sizeof(mvdev_packet_mheader))
#define MV_HEADER_CRC_OFFSET_SIZE (sizeof(mvdev_packet_mheader32))
#define MV_HEADER_OFFSET_SIZE (mvdev_header_size)

#define MV_HEADER_OFFSET_V(_v) ((_v->header == 1) ? MV_HEADER_OFFSET_SIZE : 0)
#define MV_TAIL_OFFSET_V(_v) ((_v->transport == MVDEV_TRANSPORT_RCFP) ? sizeof(MVBUF_TAIL_FLAG_TYPE) : 0)
#define MV_TRANSPORT_OFFSET(_v) ((_v->transport == MVDEV_TRANSPORT_UD) ? 40 : (_v->transport == MVDEV_TRANSPORT_RCFP ? sizeof(MVBUF_HEAD_FLAG_TYPE) : 0))
#define SBUF_MAX_DATA_SZ(_v) (_v->max_data_size - MV_TRANSPORT_OFFSET(_v) - MV_HEADER_OFFSET_V(_v) - MV_TAIL_OFFSET_V(_v))
#define SBUF_MAX_DATA_SZ_HDR(_v, type) (_v->max_data_size - sizeof(type) - MV_TRANSPORT_OFFSET(_v) - MV_HEADER_OFFSET_V(_v) - MV_TAIL_OFFSET_V(_v))

#define MV_SBUF_SET_DATA(_v, type) { \
    _v->data_ptr = _v->header_ptr + sizeof(type);  \
    _v->byte_len_data = _v->byte_len - sizeof(type); \
}


void MV_Allocate_Vbufs(int nvbufs);


void deallocate_mv_bufs(void);
void allocate_mv_bufs(int nvbufs);
mv_buf * get_mv_buf(int length_requested);
mv_sbuf *get_mv_sbuf(int length_requested);
mv_rbuf *get_mv_rbuf(int length_requested);
int release_mv_sbuf(mv_sbuf * v);
int release_mv_rbuf(mv_rbuf * v);
void release_mv_buf(mv_buf * b);

void release_mv_buf_s(mv_sbuf * v);
void release_mv_desc_s(mv_sbuf * v);

void init_vbuf_lock();
void check_rbufs();

mv_rbuf_region * allocate_mv_rbuf_region(int nvbufs, int thread);
mv_sbuf_region * allocate_mv_sbuf_region(int nvbufs, int thread);
mv_buf_region * allocate_mv_buf_region(int nvbufs, int size, int thread);

mv_sdescriptor_chunk * get_sdescriptor_chunk(int desc_count);
void release_sdescriptor_chunk(mv_sdescriptor_chunk * chunk);

/* RCFP macros */
#define BIT_MASK(_start, _end) ((( (uint64_t) 1 << (_end-_start+1) ) - 1) << _start)
#define BIT_MASK32(_start, _end) ((( (uint32_t) 1 << (_end-_start+1) ) - 1) << _start)
#define RCFP_MASK_HEAD         BIT_MASK(0,0)
#define RCFP_MASK_SIZE         BIT_MASK(1,15)
#define RCFP_MASK_SEQ          BIT_MASK(16,31)

#define PUT_IMM_MASK_RANK      BIT_MASK32(0,20)
#define PUT_IMM_MASK_TAG       BIT_MASK32(21,31)

#define RCFP_MASK_TYPE         BIT_MASK(0,6)
#define RCFP_MASK_LAST_RECV    BIT_MASK(7,22)
#define RCFP_MASK_TAIL         BIT_MASK(23,23)
#define RCFP_MASK_RDMA_CREDIT  BIT_MASK(24,31)

#define RCFP_OFFSET_HEAD 0
#define RCFP_OFFSET_SIZE 1
#define RCFP_OFFSET_SEQ 16

#define PUT_IMM_OFFSET_RANK 0
#define PUT_IMM_OFFSET_TAG 21

#define RCFP_OFFSET_TAIL 23
#define RCFP_OFFSET_TYPE 0
#define RCFP_OFFSET_LAST_RECV 7
#define RCFP_OFFSET_RDMA_CREDIT 24

#define PUT_IMM_BITS_RANK(_head) ((_head << PUT_IMM_OFFSET_RANK) & PUT_IMM_MASK_RANK)
#define PUT_IMM_BITS_TAG(_head) ((_head << PUT_IMM_OFFSET_TAG) & PUT_IMM_MASK_TAG)

#define RCFP_BITS_HEAD(_head) ((_head << RCFP_OFFSET_HEAD) & RCFP_MASK_HEAD)
#define RCFP_BITS_SIZE(_head) ((_head << RCFP_OFFSET_SIZE) & RCFP_MASK_SIZE)
#define RCFP_BITS_SEQ(_head) ((_head << RCFP_OFFSET_SEQ) & RCFP_MASK_SEQ)
#define RCFP_BITS_TAIL(_head) ((_head << RCFP_OFFSET_TAIL) & RCFP_MASK_TAIL)
#define RCFP_BITS_TYPE(_head) ((_head << RCFP_OFFSET_TYPE) & RCFP_MASK_TYPE)
#define RCFP_BITS_LAST_RECV(_head) ((_head << RCFP_OFFSET_LAST_RECV) & RCFP_MASK_LAST_RECV)
#define RCFP_BITS_RDMA_CREDIT(_head) ((_head << RCFP_OFFSET_RDMA_CREDIT) & RCFP_MASK_RDMA_CREDIT)

#define CREATE_RCFP_HEADER(_head, _size, _seq)    \
    (RCFP_BITS_HEAD(_head) | RCFP_BITS_SIZE(_size) | RCFP_BITS_SEQ(_seq))

#define CREATE_RCFP_FOOTER(_tail, _type, _last_recv)    \
    (RCFP_BITS_TAIL(_tail) | RCFP_BITS_TYPE(_type) | \
     RCFP_BITS_LAST_RECV(_last_recv))

#define CREATE_PUT_IMM(_rank, _tag) \
    (PUT_IMM_BITS_RANK(_rank) | PUT_IMM_BITS_TAG(_tag))

#define PUT_IMM_GBITS_RANK(_head) (((_head) & PUT_IMM_MASK_RANK) >> PUT_IMM_OFFSET_RANK);
#define PUT_IMM_GBITS_TAG(_head) (((_head) & PUT_IMM_MASK_TAG) >> PUT_IMM_OFFSET_TAG);

#define RCFP_GBITS_HEAD(_head) (((_head) & RCFP_MASK_HEAD) >> RCFP_OFFSET_HEAD);
#define RCFP_GBITS_SIZE(_head) (((_head) & RCFP_MASK_SIZE) >> RCFP_OFFSET_SIZE);
#define RCFP_GBITS_SEQ(_head) (((_head) & RCFP_MASK_SEQ) >> RCFP_OFFSET_SEQ);
#define RCFP_GBITS_TAIL(_head) (((_head) & RCFP_MASK_TAIL) >> RCFP_OFFSET_TAIL);
#define RCFP_GBITS_TYPE(_head) (((_head) & RCFP_MASK_TYPE) >> RCFP_OFFSET_TYPE);
#define RCFP_GBITS_LAST_RECV(_head) (((_head) & RCFP_MASK_LAST_RECV) >> RCFP_OFFSET_LAST_RECV);
#define RCFP_GBITS_RDMA_CREDIT(_head) (((_head) & RCFP_MASK_RDMA_CREDIT) >> RCFP_OFFSET_RDMA_CREDIT);

#define READ_RCFP_HEADER(_val, _head, _size, _seq) { \
    *(_head) = RCFP_GBITS_HEAD((_val)); \
    *(_size) = RCFP_GBITS_SIZE((_val)); \
    *(_seq) = RCFP_GBITS_SEQ((_val)); \
}

#define READ_PUT_IMM(_val, _rank, _tag) { \
    *(_rank) = PUT_IMM_GBITS_RANK((_val)); \
    *(_tag) = PUT_IMM_GBITS_TAG((_val)); \
}

#define READ_RCFP_FOOTER(_val, _tail, _type, _last_recv) { \
    *(_tail) = RCFP_GBITS_TAIL((_val)); \
    *(_type) = RCFP_GBITS_TYPE((_val)); \
    *(_last_recv) = RCFP_GBITS_LAST_RECV((_val)); \
}

#define READ_RCFP_TAIL_VAL(_val, _tail) { \
    *(_tail) = RCFP_GBITS_TAIL((_val)); \
}

#endif                          /* _VBUF_H */

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

#ifndef _VBUF_H
#define _VBUF_H

#include <pthread.h>
#include "ibverbs_header.h"
#include "viaparam.h"
#include "viapacket.h"

#ifdef _IA64_
#define VBUF_FLAG_TYPE uint64_t
#else
#define VBUF_FLAG_TYPE uint32_t
#endif

#if (defined(RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH))

#define VBUF_BUFFER_SIZE (viadev_vbuf_total_size -    \
     2*sizeof(VBUF_FLAG_TYPE))

#else

#define VBUF_BUFFER_SIZE  (viadev_vbuf_total_size)

#endif

/*
 * brief justification for vbuf format:
 * descriptor must be aligned (64 bytes).
 * vbuf size must be multiple of this alignment to allow contiguous allocation
 * descriptor and buffer should be contiguous to allow via implementations that
 * optimize contiguous descriptor/data (? how likely ?)
 * need to be able to store send handle in vbuf so that we can mark sends
 * complete when communication completes. don't want to store
 * it in packet header because we don't always need to send over the network.
 * don't want to store at beginning or between desc and buffer (see above) so
 * store at end. 
 */

struct ibv_descriptor {
    union {
        struct ibv_recv_wr rr;
        struct ibv_send_wr sr;
    } u;
    struct ibv_sge sg_entry;
    void *next;
};

typedef struct ibv_descriptor IBV_DESCRIPTOR;

typedef struct vbuf {

#if (defined(RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH))
    VBUF_FLAG_TYPE *head_flag;
#endif

    unsigned char *buffer;

#if (defined(RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH))
    int padding;
#endif

    IBV_DESCRIPTOR desc;

    /* NULL shandle means not send or not complete. Non-null
     * means pointer to send handle that is now complete. Used
     * by viadev_process_send
     */
    void *shandle;
    struct vbuf_region *region;
    int grank;

    uint16_t bytes_remaining;
    uint16_t len;
    unsigned char *data_start;
    uint16_t ref_count;

    /* NR */
    uint8_t ib_completed;
    uint8_t sw_completed;
    struct vbuf *next;
    struct vbuf *prev;
#ifndef DISABLE_HEADER_CACHING
    viadev_packet_eager_start match_hdr;
    /* This header is just a dummy header which is filled
     * before a match is made */
#endif
} vbuf;
/* one for head and one for tail */
#define VBUF_FAST_RDMA_EXTRA_BYTES (2*sizeof(VBUF_FLAG_TYPE))

#define FAST_RDMA_ALT_TAG 0x8000
#define FAST_RDMA_SIZE_MASK 0x7fff

/*
 * Vbufs are allocated in blocks and threaded on a single free list.
 *
 * These data structures record information on all the vbuf
 * regions that have been allocated.  They can be used for
 * error checking and to un-register and deallocate the regions
 * at program termination.
 *
 */
typedef struct vbuf_region {
    struct ibv_mr *mem_handle;  /* memory handle for entire region */

    void *malloc_start;         /* used to free region later */
    void *malloc_end;           /* to bracket mem region */
    void *malloc_buf_start;     /* used to free DMA region later */
    void *malloc_buf_end;       /* bracket DMA region */
    int count;                  /* number of vbufs in region */
    struct vbuf *vbuf_head;     /* first vbuf in region */
    struct vbuf_region *next;   /* thread vbuf regions */
} vbuf_region;

/* ack. needed here after vbuf is defined. need to clean up header files
 * and dependencies.
 */

#include "viutil.h"


void dump_vbuf_regions(void);
void dump_vbuf_region(vbuf_region * r);


void allocate_vbufs(int nvbufs);
void re_register_vbufs();
void deallocate_vbufs(void);

vbuf *get_vbuf(void);
void release_vbuf(vbuf * v);
void vbuf_init_send(vbuf * v, unsigned long len);
void vbuf_init_recv(vbuf * v, unsigned long len);
void vbuf_init_sendrecv(vbuf * v, unsigned long len);

void vbuf_init_rput(vbuf * v,
                    void *local_address,
                    uint32_t local_memhandle,
                    void *remote_address,
                    uint32_t remote_memhandle_rkey, int nbytes);
void vbuf_init_rget(vbuf * v,
                    void *local_address,
                    uint32_t local_memhandle,
                    void *remote_address,
                    uint32_t remote_memhandle_rkey, int nbytes);

void init_vbuf_lock();

void dump_vbuf(char *msg, vbuf * v);

/* 
 * Macros for working with vbufs 
 */

#define VBUF_BUFFER_START(v) (v->buffer)

#define VBUF_DATA_SIZE(type) (VBUF_BUFFER_SIZE - sizeof(type))

#endif                          /* _VBUF_H */

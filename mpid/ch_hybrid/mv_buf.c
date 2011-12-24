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

#define _XOPEN_SOURCE 600

#include "ibverbs_header.h"
#include "mv_buf.h"
#include "mv_priv.h"
#include "dreg.h"
#include "math.h"

mv_buf_size mv_buf_avail[10];
int mv_buf_avail_num = 0;
mv_buf_size mv_buf_size_fp;

mv_sdescriptor_size mv_sdescriptor_avail[10];
int mv_sdescriptor_avail_num = 0;

mv_buf_region *mv_buf_region_head = NULL;
mv_rbuf_region *mv_rbuf_region_head = NULL;
mv_sbuf_region *mv_sbuf_region_head = NULL;

mv_rbuf *mv_rbuf_head = NULL;
mv_sbuf *mv_sbuf_head = NULL;

static inline mv_sdescriptor_size * get_sdescriptor_size(int desc_count) 
{
    int i; 
    mv_sdescriptor_size * b = NULL;

    for(i = 0; i < mv_sdescriptor_avail_num; i++) {
        if(mv_sdescriptor_avail[i].count == desc_count) {
            b = &(mv_sdescriptor_avail[i]);
            break;  
        }       
    }

    if(NULL == b) {
        /* TODO: re-order based on size!!! after we add dynamic allocation  */
        b = &(mv_sdescriptor_avail[mv_sdescriptor_avail_num++]);
        b->count = desc_count;
        b->head = NULL; 
    }

    return b;
}

static inline void allocate_mv_sdescriptor_region(int desc_count, int n) 
{
    int i;
    mv_sdescriptor *sdescriptors;
    mv_sdescriptor_region * r = (mv_sdescriptor_region *) malloc(sizeof(mv_sdescriptor_region));
    if(posix_memalign((void **) &(r->desc_ptr), getpagesize(), n * desc_count * sizeof(mv_sdescriptor)))
        error_abort_all(GEN_EXIT_ERR, "unable to malloc desc buffer");
    if(posix_memalign((void **) &(r->chunk_ptr), getpagesize(), n * sizeof(mv_sdescriptor_chunk)))
        error_abort_all(GEN_EXIT_ERR, "unable to malloc chunk buffer");

    r->size = get_sdescriptor_size(desc_count);
    r->array = (mv_sdescriptor_chunk *) r->chunk_ptr;
    sdescriptors = (mv_sdescriptor *) r->desc_ptr;

    MV_ASSERT(NULL == r->size->head);

    for(i = 0; i < n; i++) {
        r->array[i].size = r->size;
        r->array[i].desc = &(sdescriptors[i * desc_count]);
        r->array[i].next = NULL;
        if(i > 0) {
            r->array[i - 1].next = &( r->array[i]);
        }
    }

    r->size->head = &(r->array[0]);
}

void release_sdescriptor_chunk(mv_sdescriptor_chunk * chunk) 
{
    mv_sdescriptor_size * s = chunk->size;

    MV_ASSERT(chunk != s->head);
    chunk->next = s->head;
    s->head = chunk;

}

mv_sdescriptor_chunk * get_sdescriptor_chunk(int desc_count) 
{
    int i;
    mv_sdescriptor_chunk * chunk;
    for(i = 0; i < mv_sdescriptor_avail_num; i++) {
        if(mv_sdescriptor_avail[i].count >= desc_count ||
                i == (mv_sdescriptor_avail_num - 1)) { 

            if(NULL == mv_sdescriptor_avail[i].head) {
                allocate_mv_sdescriptor_region(mv_sdescriptor_avail[i].count, 32);
            }       

            MV_ASSERT(NULL != mv_sdescriptor_avail[i].head);

            chunk = mv_sdescriptor_avail[i].head;
            mv_sdescriptor_avail[i].head = (struct mv_sdescriptor_chunk *) chunk->next;

            return chunk;
        }       
    }
    MV_ASSERT(0);
    return NULL;
}


static inline mv_buf_size * get_buf_size(int buf_size, int thread) 
{
    int i; 
    mv_buf_size * b = NULL;

    /* RC-FP buffers should not be threaded */
    if(THREAD_NO == thread) {
        mv_buf_size_fp.alloc_size = mv_buf_size_fp.max_data_size = buf_size;
        mv_buf_size_fp.head = NULL;
        return &(mv_buf_size_fp);
    }

    for(i = 0; i < mv_buf_avail_num; i++) {
        if(mv_buf_avail[i].alloc_size == buf_size) {
            b = &(mv_buf_avail[i]);
            break;  
        }       
    }

    if(NULL == b) {
        /* TODO: re-order based on size!!! after we add dynamic allocation  */
        b = &(mv_buf_avail[mv_buf_avail_num++]);
        b->alloc_size = b->max_data_size = buf_size;
        b->head = NULL; 

    }

    return b;
}

mv_buf_region * allocate_mv_buf_region(int buf_size, int n, int thread) 
{
    mv_buf_region * r = NULL;
    mv_buf * curr_mv_buf;
    int i;

    r = (mv_buf_region *) malloc(sizeof(mv_buf_region));

    if(posix_memalign((void **) &(r->mv_buf_ptr), getpagesize(), n * sizeof(mv_buf)))
        error_abort_all(GEN_EXIT_ERR, "unable to malloc vbuf struct");
    if(posix_memalign((void **) &(r->data_ptr), getpagesize(), n * buf_size))
        error_abort_all(GEN_EXIT_ERR, "unable to malloc vbufs DMA buffer");

    memset(r->data_ptr, 0, n * buf_size);

    r->size_ptr = get_buf_size(buf_size, thread);
    r->array = (mv_buf *) r->mv_buf_ptr;
    r->count = n;

    for(i = 0; i < mvdev.num_hcas; i++) {
        r->mem_handle[i] = register_memory(i, r->data_ptr, n * buf_size);
        if(NULL == r->mem_handle[i]) {
            MV_ASSERT(0);
        }   
    }   

    /* connect the mv_bufs to the buffer region */

    curr_mv_buf = (mv_buf *) r->mv_buf_ptr;
    for(i = 0; i < n; i++) {
        curr_mv_buf[i].region = r;
        curr_mv_buf[i].ptr = (void *) (r->data_ptr + (i * buf_size));
        curr_mv_buf[i].next_ptr = NULL;
        if(0 != i) {
            curr_mv_buf[i-1].next_ptr = &(curr_mv_buf[i]);
        }
    }
    
    /* thread onto the list */
    r->next = mv_buf_region_head;
    mv_buf_region_head = r;

    if(thread == THREAD_OK) {
        MV_ASSERT(NULL == r->size_ptr->head);
        r->size_ptr->head = (mv_buf *) r->mv_buf_ptr;
    } else {
    }

    return r;
}

mv_rbuf_region * allocate_mv_rbuf_region(int nvbufs, int thread) 
{

    int i;
    struct mv_rbuf_region *r;
    mv_rbuf * cur;    

    r = (struct mv_rbuf_region *) malloc(sizeof(struct mv_rbuf_region));

    if(NULL == r)
        error_abort_all(GEN_EXIT_ERR, "Unable to malloc a new struct mv_rbuf_region");
    if(posix_memalign((void **) &(r->mv_rbuf_ptr), getpagesize(), nvbufs * sizeof(mv_rbuf)))
        error_abort_all(GEN_EXIT_ERR, "unable to malloc vbuf struct");

    r->array = cur = (mv_rbuf *) r->mv_rbuf_ptr;
    r->count = nvbufs;

    for(i = 0; i < nvbufs; i++) {
        cur[i].region = r;
        cur[i].header_ptr = cur[i].base_ptr = NULL;
        cur[i].buf = NULL;
        cur[i].desc.parent = &(cur[i]);
        cur[i].flag = MVDEV_REG_FLAG;
        cur[i].next_ptr = NULL;
        if(i > 0) {
            cur[i - 1].next_ptr = &(cur[i]);
        }
    }

    if(THREAD_OK == thread) {
        MV_ASSERT(NULL == mv_rbuf_head);
        mv_rbuf_head = (mv_rbuf *) r->mv_rbuf_ptr;
    }

    /* thread region list */
    r->next = mv_rbuf_region_head;
    mv_rbuf_region_head = r;

    return r;
}

mv_sbuf_region * allocate_mv_sbuf_region(int nvbufs, int thread) 
{
    int i;
    struct mv_sbuf_region *r = 
        (struct mv_sbuf_region *) malloc(sizeof(struct mv_sbuf_region));
    mv_sbuf * cur;    

    if(NULL == r) 
        error_abort_all(GEN_EXIT_ERR, "Unable to malloc a new struct mv_sbuf_region");
    if(posix_memalign((void **) &(r->mv_sbuf_ptr), getpagesize(), nvbufs * sizeof(mv_sbuf)))
        error_abort_all(GEN_EXIT_ERR, "unable to malloc vbuf struct");
    
    r->array = cur = (mv_sbuf *) r->mv_sbuf_ptr;
    r->count = nvbufs;

    for(i = 0; i < nvbufs; i++) {
        cur[i].region = r;
        cur[i].base_ptr = NULL;
        cur[i].buf = NULL;
        cur[i].desc.parent = &(cur[i]);
        cur[i].in_progress = 0;
        cur[i].desc_chunk = NULL;
        cur[i].next_ptr = NULL;
        if(i > 0) {
            cur[i - 1].next_ptr = &(cur[i]);
        }
    }

    if(THREAD_OK == thread) {
        MV_ASSERT(NULL == mv_sbuf_head);
        mv_sbuf_head = (mv_sbuf *) r->mv_sbuf_ptr;
    }

    /* thread region list */
    r->next = mv_sbuf_region_head;
    mv_sbuf_region_head = r;

    return r;
}


static inline void release_mv_buf_inline(mv_buf * b) 
{
    mv_buf_size * s = b->region->size_ptr;

    MV_ASSERT(b != s->head);
    b->next_ptr = s->head;
    s->head = b;
}

void release_mv_buf(mv_buf * b) 
{
    release_mv_buf_inline(b);
}

void release_mv_buf_r(mv_rbuf * v) 
{
    if(v->buf && MVDEV_RC_FP != v->flag) {
        release_mv_buf_inline(v->buf);
        v->buf = NULL;
    }
}

void release_mv_buf_s(mv_sbuf * v) 
{
    if(v->buf && MVDEV_RC_FP != v->flag) {
        release_mv_buf_inline(v->buf);
        v->buf = NULL;
    }
}

void release_mv_desc_s(mv_sbuf * v) 
{
    if(NULL != v->desc_chunk) {
        release_sdescriptor_chunk(v->desc_chunk); 
        v->desc_chunk = NULL;
    }
}

int release_mv_sbuf(mv_sbuf * v) 
{
    if(MVDEV_RC_FP != v->flag) {
        release_mv_buf_s(v);
        release_mv_desc_s(v);

        MV_ASSERT(v != mv_sbuf_head);
        MV_ASSERT(v->in_progress == 0);

        v->next_ptr = mv_sbuf_head;
        mv_sbuf_head = v;
    } else {
        mvdev_connection_t * c = &(mvdev.connections[v->rank]);
        if(MVDEV_UNLIKELY(++(c->channel_rc_fp->send_tail) >=  viadev_num_rdma_buffer))
            c->channel_rc_fp->send_tail = 0;

        MV_ASSERT(v->in_progress == 0);

        v->in_progress = 1;
    }

    /* no need to do anything the RCFP case since the other side
     * will let us know when it is free to reuse */

    return 0;
}

int release_mv_rbuf(mv_rbuf * v) 
{
    if(MVDEV_RC_FP == v->flag) {
        mvdev_connection_t * c = &(mvdev.connections[v->rank]);
        mvdev_channel_rc_fp * ch = c->channel_rc_fp;

        if(MVDEV_UNLIKELY(++(ch->recv_tail) >= viadev_num_rdma_buffer))
            ch->recv_tail = 0;
    } else {
        /* release the buffer */
        release_mv_buf_r(v);

        MV_ASSERT(v != mv_rbuf_head);
        v->next_ptr = mv_rbuf_head;
        mv_rbuf_head = v;
    }

    return 0;
}


static inline mv_buf * get_mv_buf_inline(int length_requested) 
{
    int i;
    mv_buf * v;

    for(i = 0; i < mv_buf_avail_num; i++) {
        if(MVDEV_LIKELY(mv_buf_avail[i].max_data_size >= length_requested ||
                i == (mv_buf_avail_num - 1))) { 
            /* get the head from this list... */

            if(MVDEV_UNLIKELY(NULL == mv_buf_avail[i].head)) {
                allocate_mv_buf_region(mv_buf_avail[i].alloc_size, 128, THREAD_OK);
            }       

            v = mv_buf_avail[i].head;
            mv_buf_avail[i].head = (struct mv_buf *) v->next_ptr;
            v->next_ptr = NULL;

            return v;
        }       
    }
    MV_ASSERT(0);
    return NULL;
}

mv_buf * get_mv_buf(int length_requested) 
{
    return get_mv_buf_inline(length_requested);
}

mv_rbuf *get_mv_rbuf(int length_requested) 
{
    mv_rbuf *v = NULL; 

    /* first get a sbuf */

    if(MVDEV_LIKELY(NULL == mv_rbuf_head)) {
        allocate_mv_rbuf_region(256, THREAD_OK);
    } 

    v = mv_rbuf_head;
    mv_rbuf_head = v->next_ptr;
    v->next_ptr = NULL;
    
    /* then get a buffer of the right size */
    v->buf = get_mv_buf_inline(length_requested);

    /* initialize as needed */
    v->base_ptr = v->buf->ptr;
    v->max_data_size = v->buf->region->size_ptr->max_data_size;
    v->flag = MVDEV_REG_FLAG;

    return v;
}

mv_sbuf *get_mv_sbuf(int length_requested) 
{
    mv_sbuf *v = NULL; 

    /* first get a sbuf */

    if(MVDEV_LIKELY(NULL == mv_sbuf_head)) {
        allocate_mv_sbuf_region(256, THREAD_OK);
    } 

    v = mv_sbuf_head;
    mv_sbuf_head = v->next_ptr;
    v->next_ptr = NULL;
    
    /* then get a buffer of the right size */
    v->buf = get_mv_buf_inline(length_requested);

    /* initialize as needed */
    v->base_ptr = v->header_ptr = v->buf->ptr;
    v->max_data_size = v->buf->region->size_ptr->max_data_size;
    v->left_to_send = v->in_sendwin = v->segments = v->retry_count = 0;
    v->in_progress = 1;
    v->rel_type = MV_RELIABLE_INVALID;
    v->retry_always = 0;
    v->in_sendwin = 0;
    v->retry_count = 0;

    return v;
}

void allocate_mv_bufs(int nvbufs)
{
        allocate_mv_rbuf_region(1024, THREAD_OK);
        allocate_mv_sbuf_region(1024, THREAD_OK);

        if(MVDEV_CH_RC_MULT_SRQ == mvparams.conn_type ||
                (mvparams.msrq && 
                 mvparams.conn_type == MVDEV_CH_XRC_SHARED_MULT_SRQ)) {
            allocate_mv_buf_region(128, 256, THREAD_OK);
            allocate_mv_buf_region(256, 128, THREAD_OK);
            allocate_mv_buf_region(512, 128, THREAD_OK);
            allocate_mv_buf_region(1024, 128, THREAD_OK);
            allocate_mv_buf_region(2048, 128, THREAD_OK);
            allocate_mv_buf_region(4096, 128, THREAD_OK);
            allocate_mv_buf_region(8192, 128, THREAD_OK);
        } else {
            allocate_mv_buf_region(mvparams.mtu, 128, THREAD_OK);
            allocate_mv_buf_region(4096, 128, THREAD_OK);
            allocate_mv_buf_region(mvparams.rc_buf_size, 128, THREAD_OK);
        }   


        allocate_mv_sdescriptor_region(2, 32);
        allocate_mv_sdescriptor_region(4, 32);
        allocate_mv_sdescriptor_region(8, 32);
        allocate_mv_sdescriptor_region(32, 32);
}

void deallocate_mv_bufs() 
{
    int i = 0;
    mv_rbuf_region *r_reg = mv_rbuf_region_head, *r_reg_next;
    mv_sbuf_region *s_reg = mv_sbuf_region_head, *s_reg_next;
    mv_buf_region *b_reg = mv_buf_region_head, *b_reg_next;

    while(r_reg) {
        r_reg_next = r_reg->next;
        free(r_reg->mv_rbuf_ptr);
        free(r_reg);
        r_reg = r_reg_next;
    }

    while(s_reg) {
        s_reg_next = s_reg->next;
        free(s_reg->mv_sbuf_ptr);
        free(s_reg);
        s_reg = s_reg_next;
    }

    while(b_reg) {
        for(i = 0; i < mvdev.num_hcas; i++) {
            if(b_reg->mem_handle[i] != NULL) {
                if(ibv_dereg_mr(b_reg->mem_handle[i])) {
                    fprintf(stderr, "[%d] Error in deregistration\n", mvdev.me); 
                }
            }
        }

        b_reg_next = b_reg->next;
        free(b_reg->mv_buf_ptr);
        free(b_reg->data_ptr);
        free(b_reg);
        b_reg = b_reg_next;
    }


}






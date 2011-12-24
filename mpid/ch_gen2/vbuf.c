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

#include "vbuf.h"
#include "viutil.h"
#include "nfr.h"
#include "viapriv.h"

#include "dreg.h"


/*
 * vbufs
 * 
 * vbufs provide system buffers for VMPICH. They are analogous to mbufs
 * in BSD networking. 
 * The primary motivation for vbufs is that implementing MPI on VIA
 * seems to requiring pre-posting a number of fixed-sized buffers. 
 * These buffers must be registered (pinned). Life is easier if 
 * they are all registered at once so there is only one memory 
 * handle. We manage a fixed-size pool of vbufs that are
 * allocated and pinned when a progam starts up. We manage
 * the free vbuf list as a singly linked list. 
 * 
 *  Two different ways to manage the free list as a singly-linked list. 
 *  1. head and tail pointers. Add to tail, remove from head. 
 *  2. only head pointer, treat as a stack. 
 * 
 *  #1 Eliminates contention between adding to list and removing from list 
 *   Lock-free possible?
 * 
 *  #2 Has slightly less overhead when there is no contention, and is more 
 *  likely to produce a vbuf that is already in cache. 
 * 
 *  Currently anticipate that most access near-term will be single-threaded, 
 *  so go with head only.  (#2)
 */

/* head of list of allocated vbuf regions */
static vbuf_region *vbuf_region_head = NULL;

/* 
 * free_vbuf_head is the head of the free list
 */

static vbuf *free_vbuf_head = NULL;

static int vbuf_n_allocated = 0;
static long num_free_vbuf = 0;
static long num_vbuf_get = 0;
static long num_vbuf_free = 0;

static pthread_spinlock_t vbuf_lock;

void init_vbuf_lock()
{
    pthread_spin_init(&vbuf_lock, 0);
}

static void lock_vbuf()
{
    pthread_spin_lock(&vbuf_lock);
    return;
}

static void unlock_vbuf()
{
    pthread_spin_unlock(&vbuf_lock);
    return;
}


void dump_vbuf_region(vbuf_region * r)
{
}

void dump_vbuf_regions()
{
    vbuf_region *r = vbuf_region_head;

    while (r) {
        dump_vbuf_region(r);
        r = r->next;
    }
}
void deallocate_vbufs()
{
    vbuf_region *r = vbuf_region_head;

    lock_vbuf();

    while (r) {
        if (r->mem_handle != NULL) {
            if(ibv_dereg_mr(r->mem_handle)) {
                error_abort_all(GEN_ASSERT_ERR,
                                    "could not deregister MR");
            }
            /* free vbufs add it later */
        }
        D_PRINT("deregister vbufs\n");
        r = r->next;
    }

    unlock_vbuf();
}

void re_register_vbufs()
{
    vbuf_region *r = vbuf_region_head;

    lock_vbuf();

    while (r) {
        r->mem_handle = register_memory(r->malloc_buf_start,
                r->count * viadev_vbuf_total_size);
        r = r->next;
    }

    unlock_vbuf();
}

static void allocate_vbuf_region(int nvbufs)
{
    struct vbuf_region *reg;
    void *mem;
    void *vbuf_dma_buffer;

    int i;
    vbuf *cur;
    int alignment_vbuf = 64;
    int alignment_dma;

    alignment_dma = getpagesize();

    if (free_vbuf_head != NULL) {
        error_abort_all(GEN_ASSERT_ERR, "free_vbuf_head = NULL");
    }

    if (nvbufs <= 0) {
        error_abort_all(GEN_ASSERT_ERR, "Internal Error requested "
                "region size = %d", nvbufs);
    }
    
    /* are we limiting vbuf allocation?  If so, make sure
     * we dont alloc more than allowed
     */
    if (viadev_vbuf_max > 0) {
        nvbufs = MIN(nvbufs, viadev_vbuf_max - vbuf_n_allocated);
        if (nvbufs <= 0) {
            error_abort_all(GEN_EXIT_ERR,
                                "VBUF alloc failure, limit exceeded");
        }
    }

    reg = (struct vbuf_region *) malloc(sizeof(struct vbuf_region));
    if (NULL == reg) {
        error_abort_all(GEN_EXIT_ERR,
                            "Unable to malloc a new struct vbuf_region");
    }

    if(posix_memalign((void **) &mem, alignment_vbuf, nvbufs * sizeof(vbuf))) {
        error_abort_all(GEN_EXIT_ERR, 
                "unable to malloc vbuf struct");
    }

    /* ALLOCATE THE DMA BUFFER */

    if(posix_memalign((void **) &vbuf_dma_buffer, alignment_dma,
                nvbufs * viadev_vbuf_total_size)) {
        error_abort_all(GEN_EXIT_ERR, 
                "unable to malloc vbufs DMA buffer");
    }

    memset(mem, 0, nvbufs * sizeof(vbuf));
    memset(vbuf_dma_buffer, 0, nvbufs * viadev_vbuf_total_size);

    vbuf_n_allocated += nvbufs;
    num_free_vbuf += nvbufs;
    reg->malloc_start = mem;

    reg->malloc_buf_start = vbuf_dma_buffer;
    reg->malloc_end = (void *) ((char *) mem + nvbufs * sizeof(vbuf));
    reg->malloc_buf_end = (void *) ((char *) vbuf_dma_buffer + 
            nvbufs * viadev_vbuf_total_size);
    
    reg->count = nvbufs;

    free_vbuf_head = (vbuf *) ((aint_t) mem);
    
    reg->vbuf_head = free_vbuf_head;

    D_PRINT("VBUF REGION ALLOCATION SZ %d TOT %d FREE %ld NF %ld NG %ld",
            nvbufs, vbuf_n_allocated, num_free_vbuf,
            num_vbuf_free, num_vbuf_get);

    reg->mem_handle = register_memory(vbuf_dma_buffer,
            nvbufs * viadev_vbuf_total_size);

    if (reg->mem_handle == NULL) {
        error_abort_all(GEN_EXIT_ERR, 
                "unable to register vbuf DMA buffer");
    }

    /* init the free list */
    for (i = 0; i < nvbufs - 1; i++) {
        cur = free_vbuf_head + i;

        cur->desc.next = free_vbuf_head + i + 1;
        cur->region = reg;

#ifdef ADAPTIVE_RDMA_FAST_PATH
        cur->head_flag = (VBUF_FLAG_TYPE *) ((char *)(vbuf_dma_buffer) + 
                (i * viadev_vbuf_total_size));
        cur->buffer = (unsigned char *) ((char *)(cur->head_flag) + 
                sizeof(VBUF_FLAG_TYPE));
#else
        cur->buffer = (unsigned char *) ((char *)(vbuf_dma_buffer) +
                (i * viadev_vbuf_total_size));
#endif
    }
    /* last one needs to be set to NULL */
    cur = free_vbuf_head + nvbufs - 1;

    cur->desc.next = NULL;

    cur->region = reg;

#ifdef ADAPTIVE_RDMA_FAST_PATH
    cur->head_flag = (VBUF_FLAG_TYPE *) ((char *)vbuf_dma_buffer + 
            ((nvbufs - 1) * viadev_vbuf_total_size));

    cur->buffer = (unsigned char *) ((unsigned char *)(cur->head_flag) + 
            sizeof(VBUF_FLAG_TYPE));
#else
    cur->buffer = (unsigned char *) ((char *)vbuf_dma_buffer + 
            ((nvbufs - 1) * viadev_vbuf_total_size));

#endif

    /* thread region list */
    reg->next = vbuf_region_head;
    vbuf_region_head = reg;

}
void allocate_vbufs(int nvbufs)
{
    /* this function is only called by the init routines.
     * cache the nic handle and ptag for later vbuf_region allocations
     */
    /* now allocate the first vbuf region */
    allocate_vbuf_region(nvbufs);
}


/* 
 * Get a vbuf off the free list 
 */

vbuf *get_vbuf(void)
{
    vbuf *v;

    lock_vbuf();

    /* 
     * It will often be possible for higher layers to recover
     * when no vbuf is available, but waiting for more descriptors
     * to complete. For now, just abort. 
     */
    if (NULL == free_vbuf_head) {
        D_PRINT("Allocating new vbuf region\n");
        allocate_vbuf_region(viadev_vbuf_secondary_pool_size);
        if (NULL == free_vbuf_head) {
            error_abort_all(GEN_EXIT_ERR,
                                "No free vbufs. Pool size %d",
                                vbuf_n_allocated);
        }
    }
    v = free_vbuf_head;
    num_free_vbuf--;
    num_vbuf_get++;

    /* this correctly handles removing from single entry free list */
    free_vbuf_head = free_vbuf_head->desc.next;
#ifdef ADAPTIVE_RDMA_FAST_PATH
    /* need to change this to RPUT_VBUF_FLAG or RGET_VBUF_FLAG later
     * if we are doing rput */
    v->padding = NORMAL_VBUF_FLAG;
#endif

    /* this is probably not the right place to initialize shandle to NULL.
     * Do it here for now because it will make sure it is always initialized.
     * Otherwise we would need to very carefully add the initialization in
     * a dozen other places, and probably miss one. 
     */
    v->shandle = NULL;

    v->ref_count = 0;
    v->len = 0;
    if(viadev_use_nfr) {
        v->ib_completed = 0;
        v->sw_completed = 0;
        v->prev = NULL;
        v->next = NULL;
    }

    v->grank = -1; /* Make sure it is not inadvertantly used anywhere */

    unlock_vbuf();

    return (v);
}

/*
 * Put a vbuf back on the free list 
 */

void release_vbuf(vbuf * v)
{

    lock_vbuf();

    /* note this correctly handles appending to empty free list */

    D_PRINT("release_vbuf: releasing %p previous head = %p",
            v, free_vbuf_head);

    assert(v != free_vbuf_head);

    v->desc.next = free_vbuf_head;

#ifdef ADAPTIVE_RDMA_FAST_PATH
    if ((v->padding != NORMAL_VBUF_FLAG)
        && (v->padding != RPUT_VBUF_FLAG)
        && (v->padding != RGET_VBUF_FLAG)) {
        error_abort_all(GEN_EXIT_ERR, "vbuf %p not correct!!! %d %d %d %d\n", 
                v, v->padding, NORMAL_VBUF_FLAG, RPUT_VBUF_FLAG, RGET_VBUF_FLAG);
    }
#endif


    free_vbuf_head = v;
    num_free_vbuf++;
    num_vbuf_free++;

    unlock_vbuf();
}


/*
 * fill in vbuf descriptor with all necessary info 
 */

#ifdef MCST_SUPPORT
void vbuf_init_msend(vbuf *, unsigned long);

void vbuf_init_msend(vbuf * v, unsigned long len)
{
    v->desc.u.sr.next = NULL;
    v->desc.u.sr.send_flags = IBV_SEND_SIGNALED;
    v->desc.u.sr.opcode = IBV_WR_SEND;
    v->desc.u.sr.wr.ud.remote_qpn = 0xFFFFFF; /*IB_MULTICAST_QP */
    v->desc.u.sr.wr.ud.ah = viadev.av_hndl;      /* Check this out .. */
    v->desc.u.sr.wr_id = (aint_t) v;
    v->desc.u.sr.num_sge = 1;
    v->desc.u.sr.sg_list = &(v->desc.sg_entry);
    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = v->region->mem_handle->lkey;
    v->desc.sg_entry.addr = (uintptr_t) (v->buffer);
    
}
#endif


void vbuf_init_send(vbuf * v, unsigned long len)
{
    v->desc.u.sr.next = NULL;
    v->desc.u.sr.send_flags = IBV_SEND_SIGNALED;
    v->desc.u.sr.opcode = IBV_WR_SEND;
    v->desc.u.sr.wr_id = (aint_t) v;
    v->desc.u.sr.num_sge = 1;
    v->desc.u.sr.sg_list = &(v->desc.sg_entry);

    v->desc.sg_entry.addr = (uintptr_t) v->buffer;
    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = v->region->mem_handle->lkey;
#ifdef ADAPTIVE_RDMA_FAST_PATH
    v->padding = NORMAL_VBUF_FLAG;
#endif
}

void vbuf_init_recv(vbuf * v, unsigned long len)
{
    v->desc.u.rr.next = NULL;
    v->desc.u.rr.wr_id = (aint_t) v;
    v->desc.u.rr.num_sge = 1;
    v->desc.u.rr.sg_list = &(v->desc.sg_entry);

    v->desc.sg_entry.addr = (uintptr_t) v->buffer;
    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = v->region->mem_handle->lkey;

#ifdef ADAPTIVE_RDMA_FAST_PATH
    v->padding = NORMAL_VBUF_FLAG;
#endif
}
void vbuf_init_sendrecv(vbuf * v, unsigned long len)
{
    error_abort_all(GEN_EXIT_ERR, "don't use vbuf_init_sendrecv, "
                        "use vbuf_init_send or vbuf_init_recv\n");
}

void vbuf_init_rput(vbuf * v, void *local_address,
                    uint32_t lkey, void *remote_address,
                    uint32_t rkey, int len)
{
    v->desc.u.sr.next = NULL;
    v->desc.u.sr.send_flags = IBV_SEND_SIGNALED;
    v->desc.u.sr.opcode = IBV_WR_RDMA_WRITE;
    v->desc.u.sr.wr_id = (aint_t) v;

    v->desc.u.sr.num_sge = 1;
    v->desc.u.sr.sg_list = &(v->desc.sg_entry);

    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = lkey;
    v->desc.sg_entry.addr = (uintptr_t) local_address;

    v->desc.u.sr.wr.rdma.remote_addr = (uintptr_t) remote_address;
    v->desc.u.sr.wr.rdma.rkey = rkey;

#ifdef ADAPTIVE_RDMA_FAST_PATH
    v->padding = RPUT_VBUF_FLAG;
#endif

    D_PRINT("RDMA write\n");
}



void vbuf_init_rget(vbuf * v,
                    void *local_address,
                    uint32_t lkey,
                    void *remote_address,
                    uint32_t rkey, int len)
{
    v->desc.u.sr.next = NULL;
    v->desc.u.sr.send_flags = IBV_SEND_SIGNALED;
    v->desc.u.sr.opcode = IBV_WR_RDMA_READ;
    v->desc.u.sr.wr_id = (aint_t) v;

    v->desc.u.sr.num_sge = 1;
    v->desc.u.sr.sg_list = &(v->desc.sg_entry);

    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = lkey;
    v->desc.sg_entry.addr = (uintptr_t) local_address;

    v->desc.u.sr.wr.rdma.remote_addr = (uintptr_t) remote_address;
    v->desc.u.sr.wr.rdma.rkey = rkey;

#ifdef ADAPTIVE_RDMA_FAST_PATH
    v->padding = RGET_VBUF_FLAG;
#endif

}

/*
 * print out vbuf contents for debugging 
 */

void dump_vbuf(char *msg, vbuf * v)
{
    int i, len;
    viadev_packet_header *header = NULL;
    header = (viadev_packet_header *) VBUF_BUFFER_START(v);
    D_PRINT("%s: dump of vbuf %p, seq#=%d, type = %d\n",
            msg, v, header->id, header->type);
    len = 100;
    for (i = 0; i < len; i++) {
        if (0 == i % 16)
            D_PRINT("\n  ");
        D_PRINT("%2x  ", (unsigned int) v->buffer[i]);
    }
    D_PRINT("\n");
    D_PRINT("  END OF VBUF DUMP\n");
}

#ifdef ADAPTIVE_RDMA_FAST_PATH
void vbuf_fast_rdma_alloc (viadev_connection_t * c, int dir) 
{
    vbuf * v;
    int vbuf_alignment = 64;
    int pagesize = getpagesize();
    int i;
    struct ibv_mr *mem_handle;

    void *vbuf_ctrl_buf = NULL;
    void *vbuf_rdma_buf = NULL;

    lock_vbuf();

    /* initialize revelant fields */
    c->rdma_credit = 0;
    c->num_no_completion = 0;

    if (viadev_num_rdma_buffer) {

        /* allocate vbuf struct buffers */
        if(posix_memalign((void **) &vbuf_ctrl_buf, vbuf_alignment,
                    sizeof(struct vbuf) * viadev_num_rdma_buffer)) {
            error_abort_all(GEN_EXIT_ERR,
                            "malloc: vbuf in vbuf_fast_rdma_alloc");
        }

        memset(vbuf_ctrl_buf, 0,
                sizeof(struct vbuf) * viadev_num_rdma_buffer);

        /* allocate vbuf RDMA buffers */
        if(posix_memalign((void **)&vbuf_rdma_buf, pagesize,
                    viadev_vbuf_total_size * viadev_num_rdma_buffer)) {
            error_abort_all(GEN_EXIT_ERR,
                            "malloc: vbuf DMA in vbuf_fast_rdma_alloc");
        }

        memset(vbuf_rdma_buf, 0,
                viadev_vbuf_total_size * viadev_num_rdma_buffer);

        /* REGISTER RDMA SEND BUFFERS */
        mem_handle = register_memory(vbuf_rdma_buf, 
                viadev_vbuf_total_size * viadev_num_rdma_buffer);

        /* Connect the DMA buffer to the vbufs */
        for (i = 0; i < viadev_num_rdma_buffer; i++) {
            v = ((vbuf *)vbuf_ctrl_buf) + i;
            v->head_flag = (VBUF_FLAG_TYPE *) 
                ( (char *)(vbuf_rdma_buf) + (i * viadev_vbuf_total_size));
            v->buffer = (unsigned char *) ((char *)(v->head_flag) + sizeof(VBUF_FLAG_TYPE));
        }

        /* Some vbuf initialization */
        for (i = 0; i < viadev_num_rdma_buffer; i++) {

            /* set global_rank */
            ((vbuf *)vbuf_ctrl_buf + i)->grank = c->global_rank;

            if (dir==0) {
                ((vbuf *)vbuf_ctrl_buf + i)->desc.next = NULL;
                ((vbuf *)vbuf_ctrl_buf + i)->padding = FREE_FLAG;
            } else {
                ((vbuf *)vbuf_ctrl_buf + i)->padding = BUSY_FLAG;
            }
            ((vbuf *)vbuf_ctrl_buf + i)->ref_count = 0;
            ((vbuf *)vbuf_ctrl_buf + i)->len = 0;

        }

        if (dir==0) {
            c->RDMA_send_buf      = vbuf_ctrl_buf;
            c->RDMA_send_buf_DMA  = vbuf_rdma_buf;
            c->RDMA_send_buf_hndl = mem_handle;
            /* set pointers */
            c->phead_RDMA_send = 0;
            c->ptail_RDMA_send = viadev_num_rdma_buffer - 1;
        } else {
            c->RDMA_recv_buf      = vbuf_ctrl_buf;
            c->RDMA_recv_buf_DMA  = vbuf_rdma_buf;
            c->RDMA_recv_buf_hndl = mem_handle;
            /* set pointers */
            c->p_RDMA_recv = 0;
            c->p_RDMA_recv_tail = viadev_num_rdma_buffer - 1;

            /* Add the connection to the RDMA polling list */
            viadev.RDMA_polling_group[viadev.RDMA_polling_group_size] = c;
            viadev.RDMA_polling_group_size++;
        }

    }

    unlock_vbuf();
}

void vbuf_rdma_address_send (viadev_connection_t * c)
{
    /* inform others about the receive 
     * RDMA buffer address and handle */

    vbuf *v;
    viadev_packet_rdma_address *h;

    v = get_vbuf();
    h = (viadev_packet_rdma_address *) VBUF_BUFFER_START(v);

    /* set up the packet */
    PACKET_SET_HEADER(h, c, VIADEV_RDMA_ADDRESS);
    h->envelope.src_lrank = c->global_rank;

    /* set address and handle */
    h->RDMA_address = c->RDMA_recv_buf_DMA;
    h->RDMA_hndl = c->RDMA_recv_buf_hndl->rkey;

#ifdef MCST_SUPPORT
    if (is_mcast_enabled) {
        /* set address and handle for UD Acks, this is for now remove later */
        h->RDMA_ACK_address = (viadev.bcast_info.ack_buffer);
        h->RDMA_ACK_hndl.rkey = viadev.bcast_info.ack_mem_hndl.rkey;
    }
#endif                          /* MCST_SUPPORT */

    /* finally send it off */
    vbuf_init_send(v, sizeof(viadev_packet_rdma_address));

    viadev_post_send(c, v);

}

#endif

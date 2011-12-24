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

#include "collutils.h"
#include "viapriv.h"


/*
 * Calculates logarithm to the base 2 of a number.
*/

int log_2(int np)
{
    int lgN, t;

    for (lgN = 0, t = 1; t < np; lgN++, t += t);

    return lgN;
}

/*
 * Calculates 2^x
 */
int power_two(int x)
{
    int pow = 1;

    while (x) {
        pow = pow * 2;
        x--;
    }

    return pow;
}

void copy_recv_addr(void *addr, struct Collbuf *coll,struct ibv_mr* mr)
{
    aint_t recv_addr;
    void *dest_addr;

    /* Cast it to integer */
    recv_addr = (aint_t) addr;

    dest_addr = (void *)((char *) coll->l_coll->buf + COLL_FLAG_SPACE);

    /* Copy the memory handle structure to this memory
     * location */

    *((uint32_t*) dest_addr) = mr->rkey;

    coll->atoa->buf_hndl_addr = dest_addr;
    coll->atoa->buf_hndl_addr_len = sizeof(uint32_t);

    dest_addr = (void *)((char *) dest_addr + sizeof(uint32_t));

    /* Set the value to the recv addr */
    *((aint_t *) dest_addr) = recv_addr;

    coll->atoa->rdma_recv_addr = dest_addr;
    coll->atoa->rdma_recv_addr_len = sizeof(aint_t);

    dest_addr = (void *) ((char *) dest_addr + sizeof(aint_t));
    *((char *) dest_addr) = ALLTOALL_RDMA_FLAG;
}

void copy_recv_addr_allgather(void *addr, struct Collbuf *coll, struct ibv_mr* mr)
{
    void *dest_addr;

    /* Adding the code for allgather */
    dest_addr = ((char *) coll->l_coll->buf + ALLGATHER_OFFSET
            + ALLGATHER_COLL_BUF_SIZE - COLL_RDMA_FLAG_SPACE - COLL_ADDR_SEND_SPACE);

    /* Copy the memory handle structure to this memory
     * location */

    *((uint32_t*) dest_addr) = mr->rkey;

    coll->allgather->buf_hndl_addr = dest_addr;
    coll->allgather->buf_hndl_addr_len = sizeof(uint32_t);

    dest_addr = (void *) ((char *) dest_addr + sizeof(uint32_t));

    /* Set the value to the recv addr */
    *((aint_t *) dest_addr) = (aint_t) addr;

    coll->allgather->rdma_recv_addr = (aint_t *)dest_addr;
    coll->allgather->rdma_recv_addr_len = sizeof(aint_t);

    dest_addr = (void *) ((char *) dest_addr + sizeof(aint_t));

    *((char *) dest_addr) = ALLGATHER_RDMA_FLAG;
}

/* A wrap around function for performing a
 * RDMA write for my collectives
 * Contiguous RDMA write
 */
void coll_rdma_write(int dest, void *local_addr,
                     int len, struct ibv_mr* local_mr,
                     void *remote_addr, uint32_t rkey)
{
    uint32_t lkey;
    vbuf *v = NULL;

    viadev_connection_t *c = NULL;

    /* Grab a descriptor */
    v = get_vbuf();
    assert(v != NULL);

    /* Get connection from 'dest' global rank */
    c = &viadev.connections[dest];
    assert(c != NULL);

    lkey = local_mr->lkey;

    viadev_rput(c, v, local_addr, lkey, remote_addr, rkey, len);
}

void copy_to_rdma_buf(void* dst, void* src, int len, int src_rank, int dst_rank)
{
	/* Copy in this format */
	/*
	  ------------------------------------------
	 |  Head  |  ...msg...	|  Tail  |
	 | (char) |  			| (char) |
	  ------------------------------------------

	 */

	/* Copy the headers */ 

	*((char*)dst + 0) = ALLGATHER_HEAD_FLAG;

	/* Copy the message */
	memcpy((char*)dst + sizeof(char), src, len);

	/* Copy the tail flag */
	*((char*)dst + sizeof(char) + len) = ALLGATHER_TAIL_FLAG;
}

/* Wait until all send descriptors on the connection are
 * available */

void coll_all_send_complete(int dest)
{
    viadev_connection_t *c = NULL;

    c = &viadev.connections[dest];

    while(c->send_wqes_avail != (int) viadev_sq_size) {
        MPID_DeviceCheck(MPID_NOTBLOCKING);
    }
}


void Next_Power_of_Degree(int N, int degree, int *next_pow)
{
    int high = 65536;
    int low = 1;
    while ((high > N) && (low < N)) {
        high /= degree;
        low *= degree;
    }
    if (high <= N) {
        if (high == N)          /* no defect, power of 2! */
            (*next_pow) = N;
        else
            (*next_pow) = high << 1;
    } else {                    /* condition low >= N satisfied */
        if (low == N)           /* no defect, power of 2! */
            (*next_pow) = N;
        else
            (*next_pow) = low;
    }
}



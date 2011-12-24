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

#if (defined(VIADEV_RPUT_SUPPORT) || defined(VIADEV_RGET_SUPPORT))

#include "mpid.h"
#include "mpiimpl.h"
#include "mpimem.h"
#include "mpipt2pt.h"
#include "mpiops.h"
#include "comm.h"
#include "viapriv.h"

#include "collutils.h"
#define INTERVAL 1000

int intra_RDMA_barrier(struct MPIR_COMMUNICATOR *comm)
{
    int rdma_barrier_id;
    char *barrier_id_str;
    volatile char *barrier_notify_array;
    int rank, size, N2_next;
    int src, dst,dst1;
    int mask;
    int mpi_errno = MPI_SUCCESS;
    int togle;
    int index;
    struct R_Collbuf *remote_coll;
    char *remote_address;
    struct R_Collbuf* remote_handle;
    struct ibv_mr* local_mr;
    int counter = 0;


    rdma_barrier_id = comm->comm_coll->collbuf->rdma_barrier_id;
    togle = comm->comm_coll->collbuf->togle;
    (void) MPIR_Comm_size(comm, &size);

    rdma_barrier_id = (rdma_barrier_id == 126) ? 1 : rdma_barrier_id + 1;
    barrier_id_str =
        (char *) comm->comm_coll->collbuf->l_coll->buf + 2 * size +
        +(rdma_barrier_id % 2) + BARRIER_OFFSET;
    barrier_id_str[0] = rdma_barrier_id;

    barrier_notify_array =
        (volatile char *) comm->comm_coll->collbuf->l_coll->buf +
        BARRIER_OFFSET;


    if (size > 1) {
        comm = comm->comm_coll;
        (void) MPIR_Comm_rank(comm, &rank);
        (void) Next_Power_of_Degree(size, 2, &N2_next);
        MPID_THREAD_LOCK(comm->ADIctx, comm);

        mask = 0x1;
        while (mask < N2_next) {
            dst = (rank + mask) % size;
	    dst1 = comm->lrank_to_grank[dst];	

            remote_coll = (comm->collbuf->r_coll);
            remote_address = (char *) (remote_coll[dst].buf) +
                2 * rank + togle + BARRIER_OFFSET;
            remote_handle = &(remote_coll[dst]);
            local_mr = comm->collbuf->l_coll->mr;

            coll_rdma_write(dst1, barrier_id_str, 1, local_mr,
                            remote_address, remote_handle->rkey);

            /* Wait for message */

            src = (rank + size - mask) % size;
            index = 2 * src + togle;
            while (barrier_notify_array[index] != rdma_barrier_id) {
                if (counter % INTERVAL == 0)
                    MPID_DeviceCheck(MPID_NOTBLOCKING);
                counter++;
            };
            counter = 0;
            mask <<= 1;
        }

        togle = (togle ^ 0x01);
        comm->collbuf->rdma_barrier_id = rdma_barrier_id;
        comm->collbuf->togle = togle;

        /* Check to see if all the sends have been completed */
        mask = 0x1;
        while (mask < N2_next) {
            dst = (rank + mask) % size;
            dst1 = comm->lrank_to_grank[dst];
            coll_all_send_complete(dst1);
            mask <<= 1;
        }


        /* Unlock for collective operation */
        MPID_THREAD_UNLOCK(comm->ADIctx, comm);
    }

    MPID_DeviceCheck(MPID_NOTBLOCKING);

    return mpi_errno;
}

#endif                          /* VIADEV_RPUT_SUPPORT || VIADEV_RGET_SUPPORT */

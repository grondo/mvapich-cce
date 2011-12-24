
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

#include "mpid_bind.h"
#include "viadev.h"
#include "viaparam.h"
#include "viapriv.h"

#include <unistd.h>

#ifdef MCST_SUPPORT

int MPID_VIA_mcst_send(void *buf, int len, MPIR_SHANDLE * s);
int MPID_VIA_mcst_recv(void *buf,
                       int len, int root, MPIR_RHANDLE * rhandle);

void MPID_McastContig(struct MPIR_COMMUNICATOR *, void *, int, int, int);

void MPID_McastContig(struct MPIR_COMMUNICATOR *comm_ptr,
                      void *buf, int len, int root, int rank)
{
    MPIR_SHANDLE *shandle;
    MPIR_RHANDLE *rhandle;
    int mpi_errno = MPI_SUCCESS;
    MPID_SendAlloc(shandle);
    MPID_RecvAlloc(rhandle);
    /* argument checking will be done by IsendContig */

    if (rank == root) {

        mpi_errno = MPID_VIA_mcst_send(buf, len, shandle);

        shandle->is_complete = 1;

        MPID_SendFree(shandle);
    } else {
        mpi_errno = MPID_VIA_mcst_recv(buf, len, root, rhandle);

        MPID_RecvFree(rhandle);
    }
}

#endif                          /* MCST SUPPORT */

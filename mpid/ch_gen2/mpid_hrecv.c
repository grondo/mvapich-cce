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
#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"


#include "mpid_pack.h"
#include "viadev.h"             /* for MPID_VIA_Irecv */

static int MPID_IrecvDatatypeFinish(void *handle)
{
    int in_position = 0, out_position = 0;
    MPIR_RHANDLE *rhandle = (MPIR_RHANDLE *) handle;

    /* xxx note: the field "start" in the via implementation is 
     * the buffer used by the device. "user_buf" is the user's buffer, 
     * if different. The is the opposite of the model implementation. oops!
     *
     * Also, rhandle used to have a msgrep field, and comm field, which are now gone. 
     * Unpack syntax is Unpack(src, dest) which is different
     * from the usual copy syntax.
     */
    MPID_Unpack(rhandle->buf, rhandle->s.count, MPID_MSGREP_UNKNOWN,
                &in_position,
                rhandle->start, rhandle->count, rhandle->datatype,
                &out_position, (struct MPIR_COMMUNICATOR *) NULL,
                rhandle->s.MPI_SOURCE, &rhandle->s.MPI_ERROR);

    rhandle->s.count = out_position;

    if (rhandle->buf)
        FREE(rhandle->buf);

    /* decrement reference count to datatype as we are no longer using it */
    MPIR_Type_free(&rhandle->datatype);

    return (0);
}

void MPID_RecvDatatype(struct MPIR_COMMUNICATOR *comm_ptr,
                       void *buf,
                       int count,
                       struct MPIR_DATATYPE *dtype_ptr,
                       int src_lrank,
                       int tag,
                       int context_id,
                       MPI_Status * status, int *error_code)
{
    /* go through IrecvDatatype so that buffer management is all 
     * in one place (including the callback)
     */

    MPIR_RHANDLE rhandle;
    MPI_Request request = (MPI_Request) & rhandle;

    MPID_RecvInit(&rhandle);

    rhandle.s.MPI_ERROR = MPI_SUCCESS;

    *error_code = 0;
    MPID_IrecvDatatype(comm_ptr, buf, count, dtype_ptr, src_lrank, tag,
                       context_id, request, error_code);
    if (!*error_code) {
        MPID_RecvComplete(request, status, error_code);
    }

}

void MPID_IrecvDatatype(struct MPIR_COMMUNICATOR *comm_ptr,
                        void *buf,
                        int count,
                        struct MPIR_DATATYPE *dtype_ptr,
                        int src_lrank,
                        int tag,
                        int context_id,
                        MPI_Request request, int *error_code)
{
    int len, contig_size;
    void *mybuf;
    MPID_Msgrep_t msgrep = MPID_MSGREP_RECEIVER;

    request->rhandle.finish = NULL;

    contig_size = MPIR_GET_DTYPE_SIZE(datatype, dtype_ptr);
    if (contig_size > 0) {
        if (Is_MPI_Bottom(buf, count, dtype_ptr)) {
            buf = MPID_Adjust_Bottom(buf, dtype_ptr);
        }
        len = contig_size * count;
        MPID_IrecvContig(comm_ptr, buf, len, src_lrank, tag, context_id,
                         request, error_code);
        return;
    }

    /* Increment reference count for this type
     */
    MPIR_Type_dup(dtype_ptr);

    mybuf = 0;
    MPID_UnpackMessageSetup(count, dtype_ptr, comm_ptr, src_lrank, msgrep,
                            (void **) &mybuf, &len, error_code);
    if (*error_code)
        return;


    /* set up the finish handler (to copy back to user buffer)
     * and information needed by finish handler
     */
    request->rhandle.finish = MPID_IrecvDatatypeFinish;
    request->rhandle.start = buf;
    request->rhandle.count = count;
    request->rhandle.datatype = dtype_ptr;

    MPID_VIA_Irecv(mybuf, len, src_lrank, tag, context_id,
                   (MPIR_RHANDLE *) request, error_code);

}

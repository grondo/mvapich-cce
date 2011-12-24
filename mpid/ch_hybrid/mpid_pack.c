/*
 *
 * Copyright (C) 1993 University of Chicago
 * Copyright (C) 1993 Mississippi State University
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Argonne National Laboratory and Mississippi State University as part of MPICH.
 * Modified at Berkeley Lab for MVICH
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


/*
 * This file has cannibalized pieces of the model adi2mpack.c and 
 * adi2pack.c. Needs to be cleaned up. 
 */


#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"

#include "mpid_pack.h"

/* 
 * This file contains the routines to support noncontiguous and heterogeneous
 * datatypes by providing routines to pack and unpack messages 
 */

void MPID_PackMessage(void *src,
                      int count,
                      struct MPIR_DATATYPE *dtype_ptr,
                      struct MPIR_COMMUNICATOR *comm_ptr,
                      int dest_grank,
                      MPID_Msgrep_t msgrep,
                      MPID_Msg_pack_t msgact,
                      void **mybuf, int *mylen, int *error_code)
{
    int position = 0;

    /* Allocate the buffer */
    MPID_Pack_size(count, dtype_ptr, msgact, mylen);

    if (*mylen > 0) {
        *mybuf = (void *) MALLOC(*mylen);
        if (!*mybuf) {
            *error_code = MPI_ERR_INTERN;
            return;
        }
        MPID_Pack(src, count, dtype_ptr, *mybuf, *mylen, &position,
                  comm_ptr, dest_grank, msgrep, msgact, error_code);
        *mylen = position;
    } else {
        *mylen = 0;
        *error_code = 0;
    }
}

void MPID_UnpackMessageSetup(int count,
                             struct MPIR_DATATYPE *dtype_ptr,
                             struct MPIR_COMMUNICATOR *comm_ptr,
                             int dest_grank,
                             MPID_Msgrep_t msgrep,
                             void **mybuf, int *mylen, int *error_code)
{
    /* Get "max" size for message */
    MPID_Pack_size(count, dtype_ptr, MPID_MSG_XDR, mylen);

    /* Allocate the buffer */
    if (*mylen) {
        *mybuf = (void *) MALLOC(*mylen);
        if (!*mybuf) {
            *error_code = MPI_ERR_INTERN;
            return;
        }
    } else {
        *mybuf = 0;
        *error_code = 0;
    }
}


void MPID_Unpack(void *src,
                 int maxcount,
                 MPID_Msgrep_t msgrep,
                 int *in_position,
                 void *dest,
                 int count,
                 struct MPIR_DATATYPE *dtype_ptr,
                 int *out_position,
                 struct MPIR_COMMUNICATOR *comm_ptr,
                 int partner, int *error_code)
{
    int act_len = 0;
    int dest_len = 0;
    int err;

    err = MPIR_Unpack(comm_ptr, (char *) src + *in_position,
                      maxcount - *in_position, count, dtype_ptr,
                      msgrep, dest, &act_len, &dest_len);
    /* Be careful not to set MPI_SUCCESS over another error code */
    if (err)
        *error_code = err;
    *in_position += act_len;
    *out_position += dest_len;
}


void MPID_Pack_size(int count,
                    struct MPIR_DATATYPE *dtype_ptr,
                    MPID_Msg_pack_t msgact, int *size)
{
    int contig_size;

#if defined(MPID_HAS_HETERO) && defined(HAS_XDR)
    /* Only XDR has a different length */
    if (msgact == MPID_MSG_XDR) {
        *size = MPID_Mem_XDR_Len(dtype_ptr, count);
    } else
#endif
    {
        contig_size = MPIR_GET_DTYPE_SIZE(datatype, dtype_ptr);
        if (contig_size > 0)
            *size = contig_size * count;
        else {
            /* Our pack routine is "tight" */
            *size = dtype_ptr->size * count;
        }
    }
}

/*
 * The meaning of the arguments here is very similar to MPI_Pack.
 * In particular, the destination buffer is specified as
 * (dest/position) with total size maxcount bytes.  The next byte
 * to write is offset by position into dest.  This is a change from
 * MPICH 1.1.0 .
 *
 * Note also that msgrep is ignored.
 */
void MPID_Pack(void *src,
               int count,
               struct MPIR_DATATYPE *dtype_ptr,
               void *dest,
               int maxcount,
               int *position,
               struct MPIR_COMMUNICATOR *comm_ptr,
               int partner,
               MPID_Msgrep_t msgrep,
               MPID_Msg_pack_t msgact, int *error_code)
{
    int (*packcontig) ANSI_ARGS((unsigned char *, unsigned char *,
                                 struct MPIR_DATATYPE *, int, void *)) = 0;
    void *packctx = 0;
    int outlen;
    int err;
#ifdef HAS_XDR
    XDR xdr_ctx;
#endif

    /* Update the dest address.  This relies on char * being bytes. */
    if (*position) {
        dest = (void *) ((char *) dest + *position);
        maxcount -= *position;
    }
    /* 
       dest and maxcount now specify the REMAINING space in the buffer.
       Position is left unchanged because this code increments it 
       relatively (i.e., it adds the number of bytes written to dest).
     */
#ifdef MPID_HAS_HETERO
    switch (msgact) {
#ifdef HAS_XDR
    case MPID_MSG_XDR:
        MPID_Mem_XDR_Init(dest, maxcount, XDR_ENCODE, &xdr_ctx);
        packctx = (void *) &xdr_ctx;
        packcontig = MPID_Type_XDR_encode;
        break;
#endif
    case MPID_MSG_SWAP:
        packcontig = MPID_Type_swap_copy;
        break;
    case MPID_MSG_OK:
        /* use default routines */
        break;
    }
#endif
    /* NOTE: MPIR_Pack2 appears to handle case where src == NULL
     * (MPI_BOTTOM) if is_contig == 0.  We are only here if
     * if is_contig == 0.
     */
    outlen = 0;
    err = MPIR_Pack2(src, count, maxcount, dtype_ptr, packcontig, packctx,
                     dest, &outlen, position);
    if (err)
        *error_code = err;

    /* If the CHANGE IN position is > maxcount, then an error has occurred.
       We need to include maxcount in the call to MPIR_Pack2. */
#if HAS_XDR
    if (packcontig == MPID_Type_XDR_encode)
        MPID_Mem_XDR_Free(&xdr_ctx);
#endif
}

void *MPID_Adjust_Bottom(void *buf, struct MPIR_DATATYPE *type)
{
    return (void *) ((char *) MPI_BOTTOM + type->indices[0]);
}

int Is_MPI_Bottom(void *buf, int len, struct MPIR_DATATYPE *type)
{
    if (((char *) buf == (char *) MPI_BOTTOM) && len > 0) {
        /* first check that MPI_BOTTOM is allowed with this type */
        switch (type->dte_type) {
        case MPIR_INDEXED:
        case MPIR_HINDEXED:
        case MPIR_STRUCT:
            return 1;
        default:
            break;
        }
    }
    /* either not MPI_BOTTOM or wrong type */
    return 0;
}

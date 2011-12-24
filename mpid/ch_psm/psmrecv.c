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


#include "mpid_bind.h"
#include "psmdev.h"
#include "psmpriv.h"
#include "psmparam.h"
#include "infinipath_header.h"


void MPID_PSM_Irecv(void *buf, int len, int src_lrank, int tag,
                    int context_id, MPIR_RHANDLE * rhandle,
                    int *error_code)
{
  uint64_t rtag=0;
  uint64_t rtagsel = MQ_TAGSEL_ALL;

  /* assume success, change if necessary */
  *error_code = MPI_SUCCESS;

  /* error check. Both MPID_Recv and MPID_Irecv go through this code */
  if ((NULL == buf) && (len > 0)) {
    *error_code = MPI_ERR_BUFFER;
    return;
  }

  rhandle->is_complete = 0;
  rhandle->can_cancel = 1;
  rhandle->len = len;
  rhandle->buf = buf;

  rtag = context_id;
  rtag = rtag << TAG_BITS;
  rtag = rtag | (tag & TAG_MASK);
  rtag = rtag << SRC_RANK_BITS;
  rtag = rtag | (src_lrank & SRC_RANK_MASK);

  if(src_lrank == MPI_ANY_SOURCE)
    rtagsel = MQ_TAGSEL_ANY_SOURCE;

  if(tag == MPI_ANY_TAG)
    rtagsel = rtagsel & MQ_TAGSEL_ANT_TAG;

  psm_mq_irecv(psmdev.mq, rtag, rtagsel, MQ_FLAGS_NONE, buf,
                  len, rhandle, &(rhandle->req));
}

void MPID_PSM_RecvComplete(MPIR_RHANDLE *rhandle)
{
  MPID_PSM_Wait((void *)(&(rhandle->req)), &(rhandle->s));
  rhandle->len = rhandle->s.count;
  RECV_COMPLETE(rhandle);
}

void MPID_PSM_RecvIcomplete(MPIR_RHANDLE *rhandle)
{
  psm_error_t retVal;
  psm_mq_status_t status;

  retVal = psm_mq_test(&(rhandle->req), &status);
  if(retVal == PSM_OK)
  {
    rhandle->len = status.nbytes;
    rhandle->s.count = status.nbytes;
    rhandle->s.MPI_SOURCE = status.msg_tag & SRC_RANK_MASK;
    rhandle->s.MPI_TAG = (status.msg_tag >> SRC_RANK_BITS) & TAG_MASK;
    RECV_COMPLETE(rhandle);
  }
}







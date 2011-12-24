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

#include "psmpriv.h"
#include "req.h"
#include "infinipath_header.h"
#include "viutil.h"


void MPID_PSM_Iprobe(int tag, int context_id, int src_lrank, int *found, 
                     MPI_Status *status)
{
  psm_error_t retVal;
  psm_mq_status_t psm_status;
  uint64_t rtag=0;
  uint64_t rtagsel = MQ_TAGSEL_ALL;

  rtag = context_id;
  rtag = rtag << TAG_BITS;
  rtag = rtag |(tag & TAG_MASK);
  rtag = rtag << SRC_RANK_BITS;
  rtag = rtag | (src_lrank & SRC_RANK_MASK);

  if(src_lrank == MPI_ANY_SOURCE)
    rtagsel = MQ_TAGSEL_ANY_SOURCE;

  if(tag == MPI_ANY_TAG)
    rtagsel = rtagsel & MQ_TAGSEL_ANT_TAG;


  retVal = psm_mq_iprobe(psmdev.mq, rtag, rtagsel, &psm_status);
  if(retVal == PSM_MQ_NO_COMPLETIONS)
  {
    *found = 0;
    return;
  }
  else if(retVal != PSM_OK)
  {
    error_abort_all(GEN_EXIT_ERR, "psm_mq_iprobe failed");
  }

  *found = 1;
 
  if (NULL == status)
  {
    return;
  }
 
  status->count = psm_status.msg_length;

  /*
     Should the error code in status->MPI_ERROR be set according to 
     psm_status.error_code
   */
  status->MPI_ERROR = MPI_SUCCESS;

  /* NOt the exact source. It is truncated to 16LSB of actual source*/
  status->MPI_SOURCE = (psm_status.msg_tag & SRC_RANK_MASK);

  /* Not the exact TAG. Truncated to 32LSB of actual TAG */
  status->MPI_TAG = (psm_status.msg_tag >> SRC_RANK_BITS) & TAG_MASK; 
}

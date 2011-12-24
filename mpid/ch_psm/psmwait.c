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

#include "infinipath_header.h"
#include "req.h"
#include "viutil.h"

void MPID_PSM_Wait(void *psm_req, MPI_Status *status)
{
  psm_mq_status_t psm_status;
  psm_error_t retVal;
  psm_mq_req_t *req;

  req = psm_req;

  retVal = psm_mq_wait((psm_mq_req_t *)psm_req, &psm_status);
  if(retVal != PSM_OK)
  {
    if(retVal != PSM_OK_NO_PROGRESS)
    {
      error_abort_all(GEN_EXIT_ERR, "psm_mq_wait() failed");
    }
  }
 
  if (NULL == status)
  {
    return;
  }
 
  status->count = psm_status.nbytes;
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

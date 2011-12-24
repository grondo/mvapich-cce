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
#include "psmpriv.h"
#include "req.h"
#include "infinipath_header.h"



int MPID_DeviceCheck(MPID_BLOCKING_TYPE blocking)
{
  psm_mq_req_t req;
  psm_mq_status_t status;
  psm_error_t retVal;
  int gotone=0;

  blocking_mode:
    retVal = PSM_OK;

  while (retVal == PSM_OK) {         /* at least success on one */

    retVal = psm_mq_ipeek(psmdev.mq, &req, &status);       

    if(retVal == PSM_OK)
    {
      gotone = 1;
      psm_mq_wait(&req, &status);

      if( (((MPIR_COMMON *)status.context)->handle_type == MPIR_SEND) ||
          (((MPIR_COMMON *)status.context)->handle_type ==MPIR_PERSISTENT_SEND) )
      {
        SEND_COMPLETE(((MPIR_SHANDLE *)status.context));
      }
      else if( (((MPIR_COMMON *)status.context)->handle_type == MPIR_RECV) || 
               (((MPIR_COMMON *)status.context)->handle_type ==MPIR_PERSISTENT_RECV) )
      {
        MPIR_RHANDLE *rhandle;
        rhandle = status.context;
        rhandle->len = status.nbytes;
        rhandle->s.count = status.nbytes;
        rhandle->s.MPI_SOURCE = status.msg_tag & SRC_RANK_MASK;
        rhandle->s.MPI_TAG = (status.msg_tag >> SRC_RANK_BITS) & TAG_MASK;
        RECV_COMPLETE(rhandle);
      }
    }

  }

  if (gotone || (blocking == MPID_NOTBLOCKING)) {
    return gotone ? MPI_SUCCESS : -1;       /* nonblock must return -1 */
  }
  
  goto blocking_mode;
}


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
#include "psmdev.h"
#include "psmpriv.h"
#include "psmparam.h"
#include "infinipath_header.h"


void MPID_PSM_Send(void *buf, int len, int src_lrank, int tag, int context_id,
                   int dest_grank)
{
  uint64_t stag=0;

  stag = context_id;
  stag = stag << TAG_BITS;
  stag = stag |(tag & TAG_MASK);
  stag = stag << SRC_RANK_BITS;
  stag = stag | (src_lrank & SRC_RANK_MASK); 

    psm_mq_send(psmdev.mq, psmdev.epaddrs[dest_grank], 
                MQ_FLAGS_NONE,  /* no flags, not a sync send */
                stag,
                buf, len);
}

void MPID_PSM_Isend(void *buf, int len, int src_lrank, int tag, int context_id,
                    int dest_grank, MPIR_SHANDLE *shandle)
{
  uint64_t stag=0;

  shandle->is_complete = 0;

  stag = context_id;
  stag = stag << TAG_BITS;
  stag = stag |(tag & TAG_MASK);
  stag = stag << SRC_RANK_BITS;
  stag = stag | (src_lrank & SRC_RANK_MASK);

  psm_mq_isend(psmdev.mq, psmdev.epaddrs[dest_grank], MQ_FLAGS_NONE,
               stag, buf, len, shandle, &(shandle->req));
}

void MPID_PSM_SendComplete(MPIR_SHANDLE *shandle)
{
  MPID_PSM_Wait((void *)(&(shandle->req)), NULL);
  SEND_COMPLETE(shandle);
}

int MPID_PSM_SendIcomplete(MPIR_SHANDLE *shandle)
{
  psm_error_t retVal;

  retVal = psm_mq_test(&(shandle->req), NULL); 
  if(retVal == PSM_OK)
  {
    SEND_COMPLETE(shandle);
    return 1;
  }
  else 
  {
    return 0;
  }
}

void MPID_PSM_Ssend(void *buf, int len, int src_lrank, int tag, int context_id,
                    int dest_grank)
{
  uint64_t stag=0;

  stag = context_id;
  stag = stag << TAG_BITS;
  stag = stag |(tag & TAG_MASK);
  stag = stag << SRC_RANK_BITS;
  stag = stag | (src_lrank & SRC_RANK_MASK);

  psm_mq_send(psmdev.mq, psmdev.epaddrs[dest_grank],
              PSM_MQ_FLAG_SENDSYNC,
              stag,
              buf, len);
}

void MPID_PSM_Issend(void *buf, int len, int src_lrank, int tag, int context_id,
                     int dest_grank, MPIR_SHANDLE *shandle)
{
  uint64_t stag=0;

  shandle->is_complete = 0;

  stag = context_id;
  stag = stag << TAG_BITS;
  stag = stag |(tag & TAG_MASK);
  stag = stag << SRC_RANK_BITS;
  stag = stag | (src_lrank & SRC_RANK_MASK);

  psm_mq_isend(psmdev.mq, psmdev.epaddrs[dest_grank], PSM_MQ_FLAG_SENDSYNC,
               stag, buf, len, shandle, &(shandle->req));
}



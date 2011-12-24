
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

#ifndef _BCAST_INFO_H
#define _BCAST_INFO_H

extern unsigned long viadev_sq_size;

#include "vbuf.h"

#define NUM_COROOTS ((viadev.np % 8 == 0) ? (viadev.np/8) : 1)
#define NUM_CHILD ((viadev.np)/NUM_COROOTS)
#define CORANK(rank) (rank/NUM_CHILD)
#define CHILD(rank) (rank % NUM_CHILD)
#define NUM_WBUFFERS 2000
#define ACK_LEN 4
#define CORANK_TO_RANK(corank) ((corank * NUM_CHILD) + (viadev.me % NUM_CHILD))
#define CHILD_TO_RANK(chld) (chld + (viadev.me/NUM_CHILD)*NUM_CHILD)
#define MAX_PROCS   (2048)
#define SENDER_WINDOW 4096
#define BCAST_TIME_OUT	1000000
#define VIADEV_UD_PREPOST_DEPTH 32
#define VIADEV_UD_PREPOST_THRESHOLD 16
#define PERIOD  500
#define MAX_ACKS (100)
#define LEFT_OVERS (viadev.np % NUM_CHILD)
#define LAST_NODE ((NUM_COROOTS * NUM_CHILD)-1)
#define LAST_RANK (NUM_CHILD + (viadev.me % NUM_CHILD))

/* Macros to direct the transmission to the coroot*/

/*#define DIRECT_COROOT*/

#endif

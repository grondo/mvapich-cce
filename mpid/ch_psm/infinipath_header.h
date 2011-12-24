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


#ifndef _INFINIPATH_H
#define _INFINIPATH_H

#include <sys/types.h>
#include <inttypes.h>
#include <psm.h>
#include <psm_mq.h>

#define MQ_FLAGS_NONE  0
#define MQ_TAGSEL_ALL  0xffffffffffffffff
#define SEC_IN_NS   1000000000ULL

#define TAG_BITS 32
#define SRC_RANK_BITS 16
#define TAG_MASK ~(MQ_TAGSEL_ALL << TAG_BITS)
#define SRC_RANK_MASK ~(MQ_TAGSEL_ALL << SRC_RANK_BITS)
#define MQ_TAGSEL_ANY_SOURCE (MQ_TAGSEL_ALL << SRC_RANK_BITS)
#define MQ_TAGSEL_ANT_TAG ~(TAG_MASK << SRC_RANK_BITS)

#define STRDUP(a)       strdup(a)
#endif                          /* _INFINIPATH_H */


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
 * MPISPAWN INTERFACE FOR BUILDING DYNAMIC SOCKET TREES
 */
#ifndef MPISPAWN_TREE_H
#define MPISPAWN_TREE_H

#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>

#define MPISPAWN_PMGR_ERROR -1
#define MPISPAWN_PMGR_ABORT -2
#define MPISPAWN_RANK_ERROR -3
#define MPISPAWN_PROCESS_ABORT  -5
#define MPISPAWN_MEM_ERROR  -6

#define MAX_HOST_LEN    256
#define MAX_PORT_LEN    8

#define MT_MAX_LEVEL    4
#define MT_MIN_DEGREE   4
#define MT_MAX_DEGREE   64

typedef struct {
    int rank;
    int fd;
} child_t;
#define child_s sizeof (child_t)

int mpispawn_tree_init(size_t me, int req_socket);
int * mpispawn_tree_connect(size_t root, size_t degree);
void mpispawn_abort (int code);
int mtpmgr_init (void);
int mtpmgr_processops (void);

#ifdef MT_ASSERT_MODE 
#define MT_ASSERT(cond) do { \
    if (!(cond)) { \
        mtpmgr_error ("\n%s:%d Assert failed (%s)\n", __FILE__, \
                __LINE__, #cond); \
    }\
} while (0); 
#else
#define MT_ASSERT(cond)
#endif

#endif

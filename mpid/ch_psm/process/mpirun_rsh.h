#ifndef _MPIRUN_RSH_H
#define _MPIRUN_RSH_H 1
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

/* if this is defined, we will use the VI network for
 * the termination barrier, and not the socket based
 * barrier.
 */
#if 0
#define USE_VIADEV_BARRIER  1
#endif

#define BASE_ENV_LEN	    17
#define COMMAND_LEN	    2000

#define ENV_CMD		    "/usr/bin/env"
#define RSH_CMD		    "/usr/bin/rsh"
#define SSH_CMD		    "/usr/bin/ssh"
#define  SH_CMD             "/bin/bash"
#define XTERM		    "/usr/X11R6/bin/xterm"
#define  SH_ARG             "-c"
#define SSH_ARG		    "-q"
#define RSH_ARG		    "NOPARAM"

#ifndef LD_LIBRARY_PATH_MPI
#define LD_LIBRARY_PATH_MPI "/usr/mvapich/lib/shared"
#endif

#ifdef USE_DDD
#define DEBUGGER	    "/usr/bin/ddd"
#else
#define DEBUGGER	    "/usr/bin/gdb"
#endif

#define _GNU_SOURCE
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#ifdef MAC_OSX
#include <sys/wait.h>
#else
#include <wait.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <netdb.h>
#include <assert.h>
#include <libgen.h>
#include "mpirun_util.h"
#include "pmgr_collective_common.h"

#define PMGR_VERSION PMGR_COLLECTIVE

typedef enum {
    P_NOTSTARTED,
    P_STARTED,
    P_CONNECTED,
    P_DISCONNECTED,
    P_RUNNING,
    P_FINISHED,
    P_EXITED
} process_state;

typedef struct {
    char *hostname;
    char *device;
    pid_t pid;
    pid_t remote_pid;
    int port;
    int control_socket;
    process_state state;
} process;

typedef struct {
    const char * hostname;
    pid_t pid;
    int * plist_indices;
    size_t npids, npids_allocated;
} process_group;

typedef struct {
    process_group * data;
    process_group ** index;
    size_t npgs, npgs_allocated;
} process_groups;

#define RUNNING(i) ((plist[i].state == P_STARTED ||                 \
            plist[i].state == P_CONNECTED ||                        \
            plist[i].state == P_RUNNING) ? 1 : 0)

/* other information: a.out and rank are implicit. */

#define COMMAND_LEN 2000
#define SEPARATOR ':'

#ifndef PARAM_GLOBAL
#define PARAM_GLOBAL "/etc/mvapich.conf"
#endif

#define TOTALVIEW_CMD "/usr/totalview/bin/totalview"

#define MVAPICH_VERSION "1.2"

#ifndef MVAPICH_BUILDID
#define MVAPICH_BUILDID "custom"
#endif

#endif

/* vi:set sw=4 sts=4 tw=80: */

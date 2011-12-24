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
 */
 
#ifndef _PMGR_CLIENT_H
#define _PMGR_CLIENT_H

/*
 * This file defines the process manager interface from MVICH.
 * Any process manager that wishing to launch MVICH applications
 * must implement the following functions:
 */

void pmgr_init_connection(int phase);

int pmgr_exchange_hostid (int, int, int*);
int pmgr_exchange_hca_and_hostid (int hostid, int hostidlen, 
        int *allhostids, int hca_type , int *is_homogeneous);

/*
 * This function is called by each process in the job during
 * initialization.  Pointers to argc and argv are passes
 * in the event that the process manager passed args on
 * the command line.
 * The process manager must return the following values:
 *    *np_p     = total number of processes in the job
 *    *me_p     = the rank of this process (zero based)
 *    *id_p     = the global ID associated with this job.
 *
 * Returns: 1 = success, 0 = failure
 */
int pmgr_client_init(int *argc_p, char ***argv_p, int *np_p, 
	int *me_p, int *id_p, char ***processes_p);

/*
 * Exchange NIC Addresses with all other processes in the job.
 * Args:
 *    (in)  localaddr    memory location containing addr of our NIC
 *    (in)  addrlen      length of above string
 *    (out) alladdrs     memory large enough to hold addresses
 *                       of all NICs of all processes.
 *    (out) allpids      memory to hold pid of all processes
 *
 * The process manager communicates our address to all other
 * processes in the job.  It also collects the addresses of
 * all NICs in the alladdrs such that the NIC address of
 * process rank x is alladdrs[x] cast to the appropriate type.
 * Note: all addresses are assumed to be the same size.
 *
 * Returns: 1 = success, 0 = failure
 */
int pmgr_exchange_addresses(void *localaddr, int addrlen, 
	void *alladdrs, void *pallpids);

/*
 * Some process managers have the ability to implement a barrier
 * synchronozation that scales well with the number of processes
 * in the application.  If so, pmgr_barrier, should call this
 * and return success (1) when complete.  If not, it should just
 * return 0 and let MVICH perform the barrier using the VI network.
 *
 * NOTE: this is not used for general MPI_Barrier calls, only in
 *       special startup/shutdown situations at which MPI is
 *       not fully available.
 *
 * Returns: 1 = success, 0 = failure
 */
int pmgr_barrier(void);

/*
 * Some process managers (MPD) may have allocated data structures
 * to assist in the management of this application.  Allow it
 * to perform cleanup actions before we terminate.
 * Returns: 1 = success, 0 = failure
 */
int pmgr_finalize(void);

int pmgr_abort(int);
int pmgr_get_mpirun_process(int np,char ***processes_p);

#ifdef MCST_SUPPORT
#define GRP_INFO_LEN 8
int pmgr_exchange_mcs_group_sync(void *grp_info, int grp_info_len,
                                 int root);
#endif

/*
 * Bump this every time you change the init protocol between the
 * process spawner and the parallel code.  It is the responsibility
 * of the spawner, e.g. mpirun_rsh, to check that it understands
 * the version of the executable.
 */
#define PMGR_VERSION 8

#endif

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

#define _XOPEN_SOURCE 600

#if defined(_SMP_) || defined(USE_MPD_BASIC) || defined(USE_MPD_RING)
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#endif

/* Required for gethostbyname, which is needed for
 * multi-port, multi-hca */
#include <netdb.h>

#ifdef _SMP_
#ifdef _AFFINITY_
#include <sched.h>
#endif /*_AFFINITY_*/
#endif

#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include "viadev.h"
#include "viaparam.h"
#include "viapriv.h"
#include "viutil.h"

#include "process/pmgr_collective_client.h"
#include "mpid_smpi.h"

#ifdef USE_MPD_RING
static void ib_vapi_bootstrap_ring(int, int, char *, char *, char *,
                                   struct ibv_mr *);
static void ib_vapi_enable_ring(int, int, char *);
#endif

#ifdef _SMP_
#ifdef _AFFINITY_
extern unsigned int viadev_enable_affinity;
#endif
#endif

static void ib_rank_lid_table(int *alladdrs);

#ifdef USE_MPD_RING
static void *barrier_buff;
#endif

static int get_host_id(char *myhostname, int hostname_len);
static void viainit_setaffinity(char *cpu_mapping);

viadev_info_t viadev;

int vapi_proc_info(int i, char **host, char **exename)
{
    int pid;
    pid = viadev.pids[i];
    *host = viadev.processes[i];
    *exename = viadev.execname;
    return (pid);
}

int MPID_VIA_Init(int *argc, char ***argv, int *size, int *rank)
{
    int *alladdrs = NULL;
    int *local_addr = NULL;

    int hostid = 0;
    int *allhostids = NULL;
#ifdef _SMP_
#ifdef _AFFINITY_ /*_AFFINITY_ start*/
    long N_CPUs_online;

    /*Get number of CPU on machine*/
    if (( N_CPUs_online = sysconf (_SC_NPROCESSORS_ONLN)) < 1 ) {
        perror("sysconf");
    }
#endif /*_AFFINITY_ end*/
#endif

    int mypid;
    char host[256], *buf;

    /* init the VIA barrier counter */
    viadev.barrier_id = 0;

    /* Device is not yet initialized */
    viadev.initialized = 0;


    /* Get information about myself ... */
    if (PMGR_SUCCESS != pmgr_init(argc, argv, &viadev.np, &viadev.me,
                &viadev.global_id)) {
        error_abort_all(GEN_EXIT_ERR, "pmgr_client_init failed");
    }   
    pmgr_open();
    gethostname(host, sizeof(host));
    pmgr_allgatherstr(host, &viadev.processes, &buf);

    *size = viadev.np;
    *rank = viadev.me;
    if(NULL != argv) {
        strncpy(viadev.execname,
                ((const char **) (*argv))[0], VIADEV_MAX_EXECNAME);
    } else {
        strncpy(viadev.execname, "unknown_exe", VIADEV_MAX_EXECNAME);
    }
    viadev.is_finalized = 0;

    /* Now that we know our rank, we should also know
     * our name */
    viadev.my_name = (char *) malloc(sizeof(char) * HOSTNAME_LEN);

    if (NULL == viadev.my_name) {
        error_abort_all(GEN_EXIT_ERR, "malloc failed");
    }

    if (gethostname(viadev.my_name, sizeof(char) * HOSTNAME_LEN) < 0) {
        error_abort_all(GEN_EXIT_ERR, "gethostname failed");
    }

    D_PRINT("size=%d, myrank=%d\n", *size, *rank);


    /* For both SMP and no-SMP, we have the same address length */
    alladdrs = (int *) malloc(3 * viadev.np * sizeof(int));
    local_addr = (int *) malloc(2 * viadev.np * sizeof(int));

    viadev.lid_table = (uint16_t *) malloc(viadev.np * sizeof(int));
    viadev.qp_table = (uint32_t *) malloc(viadev.np * sizeof(int));
    viadev.pids = (int *) malloc(viadev.np * sizeof(int));
    mypid = (int) getpid();
    viadev.pids[viadev.me] = mypid = (int) getpid();

    if (alladdrs == NULL || local_addr == NULL ||
        viadev.lid_table == NULL || viadev.qp_table == NULL) {
        error_abort_all(GEN_EXIT_ERR,
                        "malloc for alladdrs/local_addr/lid_table/qp_table");
    }

    hostid = get_host_id(viadev.my_name, HOSTNAME_LEN);
    allhostids = (int *)malloc(viadev.np * sizeof(hostid));
    if (hostid == 0 || allhostids == NULL) {
        error_abort_all(GEN_EXIT_ERR,
                "malloc: in init");
    }
    pmgr_allgather(&hostid, sizeof(hostid), allhostids);
    pmgr_allgather(&mypid, sizeof(mypid), viadev.pids);
    
    viadev_init_parameters(viadev.np, viadev.me);
    ib_rank_lid_table(allhostids);

    /* initialize connection parameters */
    viadev.connections = (viadev_connection_t *)
        malloc(sizeof(viadev_connection_t) * viadev.np);

    if (NULL == viadev.connections) {
        error_abort_all(GEN_EXIT_ERR,
                        "Unable to malloc connection structures");
    }   

    memset(viadev.connections, 0,
           (sizeof(viadev_connection_t) * viadev.np));


    free(local_addr);
    free(alladdrs);

    pmgr_close();

    return 0;
}

/*
 * Set up all communication and put VIs in the VI array in viadev
 * The algorithm is as follows (may change when M-VIA supports peer-to-peer
 * connections):
 *   . use client-server routines to establish connections
 *   . for each pair, the lower-numbered process acts as the server
 *   . we should be careful not to establish hotspots. E.g., if
 *     every process connects with others in order 0, 1, 2. We
 *     should also be careful not to serialize. 
 *     there is a nice fancy way to do this, with a communication pattern
 *     essentially the same as in an FFT. 
 *   . However, it is a bit tricky to get right so for now 
 *     do the stupid serialized version (xxx):
 *        for (i = 0; i < me; i++) connect(i)
 *        for (i = me+1; i <= np; i++) accept(i)
 *   
 *  Note that we do not set up a connection to ourselves 
 *
 */

void MPID_Wtime(double *t)
{
    struct timeval tv;
    static int initialized = 0;
    static int sec_base;
    gettimeofday(&tv, NULL);
    if (!initialized) {
        sec_base = tv.tv_sec;
        initialized = 1;
    }
    *t = (double) (tv.tv_sec - sec_base) + (double) tv.tv_usec * 1.0e-6;
}

void MPID_Wtick(double *t)
{
    *t = VIADEV_WTICK;
}

void MPID_VIA_Finalize(void)
{
    /* free connection array space */
    if (viadev.connections) {
        free(viadev.connections);
    }

    /* let the process manager reclaim data structures 
     * before termination */

    pmgr_finalize();

    if (viadev.lid_table) {
        free(viadev.lid_table);
    }
    if (viadev.qp_table) {
        free(viadev.qp_table);
    }
    if (viadev.pids) {
        free(viadev.pids);
    }
    if (viadev.processes) {
        free(viadev.processes);
    }
#ifdef _SMP_
    if (smpi.local_nodes) {
        free(smpi.local_nodes);
    }
    if (smpi.l2g_rank) {
        free(smpi.l2g_rank);
    }
#endif

    D_PRINT("exiting MPID_VIA_Finalize...\n");

}

/* gethostname does return a NULL-terminated string if
 * the hostname length is less than "HOSTNAME_LEN". Otherwise,
 * it is unspecified.
 */
static int get_host_id(char *myhostname, int hostname_len)
{
    int host_id = 0;
    struct hostent *hostent;

    hostent = gethostbyname(myhostname);
    if (hostent == NULL) {
        error_abort_all(GEN_EXIT_ERR,
                        "Null value returned by gethostbyname");
    }

    host_id = ((struct in_addr *) hostent->h_addr_list[0])->s_addr;

    return host_id;
}

static void ib_rank_lid_table(int *alladdrs)
{
    int i;

    int *hostnames_j = NULL;
    int *smpi_ptr = NULL;
    int j;

    hostnames_j = (int *) malloc(viadev.np * sizeof(int));
    smpi.local_nodes = (unsigned int *) malloc(viadev.np * sizeof(int));
    smpi_ptr = (unsigned int *) malloc(viadev.np * sizeof(int));

    if (hostnames_j == NULL || smpi.local_nodes == NULL
        || smpi_ptr == NULL) {
        error_abort_all(GEN_EXIT_ERR,
                        "malloc: in ib_rank_lid_table for SMP");
    }

    for (i = 0; i < viadev.np; i++) {
        hostnames_j[i] = alladdrs[i];
    }

    smpi.only_one_device = 1;
    smpi.num_local_nodes = 0;
    for (j = 0; j < viadev.np; j++) {
        if (hostnames_j[viadev.me] == hostnames_j[j]) {
            if (j == viadev.me) {
                smpi.my_local_id = smpi.num_local_nodes;
            }
            smpi.local_nodes[j] = smpi.num_local_nodes;
            smpi_ptr[smpi.num_local_nodes] = j;
            smpi.num_local_nodes++;
        } else {
            error_abort_all(GEN_EXIT_ERR,
                "Fatal error: To use the ch_smp device all the processes should be on the same node"); 
            smpi.only_one_device = 0;
            smpi.local_nodes[j] = -1;
        }
    }

#ifdef _AFFINITY_
    if ( viadev_enable_affinity > 0 ){
	viainit_setaffinity(cpu_mapping);
    }
#endif /* _AFFINITY_ */

    smpi.l2g_rank =
        (unsigned int *) malloc(smpi.num_local_nodes * sizeof(int));
    if (smpi.l2g_rank == NULL) {
        error_abort_all(GEN_EXIT_ERR,
                        "malloc: in ib_rank_lid_table for SMP");
    }
    for (j = 0; j < smpi.num_local_nodes; j++) {
        smpi.l2g_rank[j] = smpi_ptr[j];
    }
    free(smpi_ptr);
    free(hostnames_j);
}

#ifdef _AFFINITY_
void viainit_setaffinity(char *cpu_mapping)
{
    long N_CPUs_online;
    unsigned long affinity_mask = 1;
    unsigned long affinity_mask_len = sizeof(affinity_mask);
    char *tp;
    char *cp;
    char tp_str[8];
    int cpu, i, j;

    /*Get number of CPU on machine */
    if ((N_CPUs_online = sysconf(_SC_NPROCESSORS_ONLN)) < 1) {
        perror("sysconf");
    }

    if (cpu_mapping) {
        /* If the user has specified how to map the processes,
         * use the mapping specified by the user
         */
        tp = cpu_mapping;
        j = 0;
        while (*tp != '\0') {
            i = 0;
            cp = tp;
            while (*cp != '\0' && *cp != ',' && *cp != ':') {
                cp++;
                i++;
            }
            strncpy(tp_str, tp, i);
            tp_str[i] = '\0';
            cpu = atoi(tp_str);

            if (j == smpi.my_local_id) {
                affinity_mask = 1 << cpu;
                if (sched_setaffinity(0,
                    affinity_mask_len, &affinity_mask)<0 ) {
		    perror("sched_setaffinity");
                }
                break; 
            }
            tp = cp + 1;
            j++;
        }
 
        free(cpu_mapping);
    } else {
        /* The user has not specified how to map the processes,
         * use the default scheme
         */
        affinity_mask = 1 << (smpi.my_local_id%N_CPUs_online);
        if (sched_setaffinity(0,affinity_mask_len,&affinity_mask)<0 ) {
	    perror("sched_setaffinity");
        }
    }
}
#endif /* _AFFINITY_ */

#ifdef PENTIUM_MHZ
double get_us(void)
{
    unsigned long long now;
    __asm __volatile(".byte 0x0f, 0x31":"=A"(now));
    return (double) ((double) now / (PENTIUM_MHZ));
}
#else
double get_us(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return (double) t.tv_sec * (double) 1e6 + (double) t.tv_usec;
}
#endif

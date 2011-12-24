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

/* Thanks to Mellanox for contributing to the Adaptive RDMA
 * implementation.
 */

#define _XOPEN_SOURCE 600

/* Required for gethostbyname, which is needed for
 * multi-port, multi-hca */
#include <netdb.h>


#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include "psmdev.h"
#include "psmparam.h"
#include "psmpriv.h"
#include "process/pmgr_collective_client.h"
#include "mpid_smpi.h"
#include "infinipath_header.h"
#include <sys/resource.h>

#define HOSTNAME_LEN                          (255)

#ifdef _SMP_
#ifdef _AFFINITY_
#include <sched.h>
#endif

extern unsigned int viadev_enable_affinity;
void viainit_setaffinity(char *);
#endif /* defined(_AFFINITY_) */

#ifdef _SMP_
#ifdef _AFFINITY_
void viainit_setaffinity(char *cpu_mapping) {
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
#endif /* defined(_AFFINITY_) */
#endif /* defined(_SMP_) */

static int get_host_id(char *myhostname);

int vapi_proc_info(int i, char **host, char **exename)
{
    int pid;
    pid = psmdev.pids[i];
    *host = psmdev.processes[i];
    *exename = psmdev.execname;
    return (pid);
}




int MPID_PSM_Init(int *argc, char ***argv, int *size, int *rank)
{
    int hostid = 0;
    int *allhostids = NULL;

    int verno_minor = PSM_VERNO_MINOR;
    int verno_major = PSM_VERNO_MAJOR;
    psm_uuid_t uuid; /* an array of 16 bytes */
    psm_epid_t my_epid;
    psm_epid_t *epid_list;
    uint64_t timeout = 20;
    psm_error_t *errors;
    char temp_str[100];
    char temp_str1[100];
    char *buf;
    int j;
    int my_local_id=0, num_local_nodes=0;

    /* Get information about myself ... */
    if (PMGR_SUCCESS != pmgr_init(argc, argv, &psmdev.np, &psmdev.me,
                &psmdev.global_id)) {
        error_abort_all(GEN_EXIT_ERR, "pmgr_client_init failed");
    }
    pmgr_open();

    *size = psmdev.np;
    *rank = psmdev.me;

    /* for IB systems, increase our memlock limit */
    struct rlimit rl;
    getrlimit(RLIMIT_MEMLOCK, &rl);
    rl.rlim_cur = rl.rlim_max;
    setrlimit(RLIMIT_MEMLOCK, &rl);

    if(NULL != argv) {
        strncpy(psmdev.execname, 
                ((const char **) (*argv))[0], PSMDEV_MAX_EXECNAME);
    } else {
        strncpy(psmdev.execname, "unknown_exe", PSMDEV_MAX_EXECNAME);
    }


    /* Now that we know our rank, we should also know
     * our name */
    psmdev.my_name = (char *) malloc(sizeof(char) * HOSTNAME_LEN);

    if (NULL == psmdev.my_name) {
        error_abort_all(GEN_EXIT_ERR, "malloc failed");
    }

    if (gethostname(psmdev.my_name, sizeof(char) * HOSTNAME_LEN) < 0) {
        error_abort_all(GEN_EXIT_ERR, "gethostname failed");
    }

    psmdev.pids = (int *) malloc(psmdev.np * sizeof(int));
    psmdev.pids[psmdev.me] = (int) getpid();
    pmgr_allgatherstr(psmdev.my_name, &psmdev.processes, &buf);

    pmgr_allgather(&(psmdev.pids[psmdev.me]), sizeof(int), psmdev.pids);

    hostid = get_host_id(psmdev.my_name);
    allhostids= (int *)malloc(psmdev.np * sizeof(hostid));
    if (hostid == 0 || allhostids == NULL) {
        error_abort_all(GEN_EXIT_ERR,
                "malloc: in init");
    }

    pmgr_allgather(&hostid, sizeof(hostid), allhostids);

    viadev_init_parameters();

#if (defined(_SMP_))
    smpi.local_nodes = (unsigned int *) malloc(psmdev.np * sizeof(int));
    smpi.l2g_rank =
                (unsigned int *) malloc(psmdev.np * sizeof(int));

    if (smpi.local_nodes == NULL || smpi.l2g_rank == NULL) {
        error_abort_all(GEN_EXIT_ERR,
                "malloc: in init");
    }
    smpi.only_one_device = 1;
    smpi.num_local_nodes = 0;

    for (j = 0; j < psmdev.np; j++) {
        if (allhostids[psmdev.me] == allhostids[j]) {
            if (j == psmdev.me) {
                smpi.my_local_id = smpi.num_local_nodes;
#ifdef _AFFINITY_
                if (viadev_enable_affinity)
                    viainit_setaffinity(cpu_mapping);
#endif
            }
            smpi.local_nodes[j] = smpi.num_local_nodes;
            smpi.l2g_rank[smpi.num_local_nodes] = j;
            smpi.num_local_nodes++;
        } else {
            smpi.only_one_device = 0;
            smpi.local_nodes[j] = -1;
        }
    }
#endif


     /* Set the two environment variables to enable more than 4 
        processes per node. This is done to resolve bug in InfiniPath 2.1 */
    if(PSM_VERNO == 0x0105)
    {
      for (j = 0; j < psmdev.np; j++) {
          if (allhostids[psmdev.me] == allhostids[j]) {
              if (j == psmdev.me) {
                  my_local_id = num_local_nodes;
              }
              num_local_nodes++;
          } 
      }

      sprintf(temp_str, "MPI_LOCALNRANKS=%d", num_local_nodes);
      putenv(temp_str);
      sprintf(temp_str1, "MPI_LOCALRANKID=%d", my_local_id);
      putenv(temp_str1);
    }

    free(allhostids);

    /*Psm Layer has a bug in shared memory in Psm 2.0 version*/
    if(PSM_VERNO <= 0x0103)
      putenv("PSM_DEVICES=shm,ipath");

    if(psm_init(&verno_major, &verno_minor)!=PSM_OK)
    {
      error_abort_all(GEN_EXIT_ERR, "Failed to initialize PSM Device");
    }

    /*
     * Process 0 generates the unique network id, 
     * send it to process manager which sends it to all.
     */
    if(*rank==0)
    {
        psm_uuid_generate(uuid);
    }

    pmgr_bcast(uuid, sizeof(psm_uuid_t),0);

#ifdef _SMP_
    smpi_init();
#endif

    /*
     * Open a new InfiniBand endpoint handle and identifing endpoint ID.
     */
    if(psm_ep_open(uuid, NULL, &psmdev.ep, &my_epid)!=PSM_OK)
    {
        error_abort_all(GEN_EXIT_ERR, "Failed to open an end-point");
    }

    epid_list = (psm_epid_t *)malloc(sizeof(psm_epid_t)*psmdev.np);

    pmgr_allgather(&my_epid, sizeof(psm_epid_t), epid_list);

    psmdev.epaddrs = (psm_epaddr_t *) malloc(sizeof(psm_epaddr_t) * psmdev.np);
    errors = (psm_error_t *) malloc(sizeof(psm_error_t) * psmdev.np);

    if(psmdev.epaddrs == NULL || errors == NULL)
    {
        error_abort_all(GEN_EXIT_ERR, "Malloc error in init.\n");
    }

    if(psm_ep_connect(psmdev.ep, psmdev.np, epid_list, NULL, errors, 
                      psmdev.epaddrs, timeout)!=PSM_OK)
    {
        error_abort_all(GEN_EXIT_ERR, "Unable to connect the end points");
    }

    free(errors); 
    free(epid_list);

    if(psm_mq_init(psmdev.ep, PSM_MQ_ORDERMASK_ALL, NULL, 0, 
                   &psmdev.mq)!=PSM_OK)
    {
        error_abort_all(GEN_EXIT_ERR, "Unable to initialize MQ");
    }

    pmgr_close();

    return 0;
}

/*
 * Set up all communication and put VIs in the VI array in psmdev
 * The algorithm is as follows (may change when M-PSM supports peer-to-peer
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
    *t = PSMDEV_WTICK;
}

void MPID_PSM_Finalize(void)
{
    psm_ep_close(psmdev.ep, PSM_EP_CLOSE_GRACEFUL, 5 * SEC_IN_NS);
    /* PSM finalize called */
    psm_finalize();

    pmgr_finalize();
}

/* internal functions used for initialization 
 * and finalization by VAPI */

/* gethostname does return a NULL-terminated string if
 * the hostname length is less than "HOSTNAME_LEN". Otherwise,
 * it is unspecified.
 */
static int get_host_id(char *myhostname)
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

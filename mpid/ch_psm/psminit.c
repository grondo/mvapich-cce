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



/* ensure that all procs have completed their call to psm_mq_init */
static int psm_mq_init_barrier(psm_mq_t mq, int rank, int ranks, psm_epaddr_t* addrs)
{
    int tmp_rc;
    int rc = PSM_OK;

    /* implement barrier dissemination algorithm */
    int dist = 1;
    while (dist < ranks) {
        /* compute rank of source for this phase */
        int src = rank - dist;
        if (src < 0) {
            src += ranks;
        }

        /* compute rank of destination for this phase */
        int dst = rank + dist;
        if (dst >= ranks) {
            dst -= ranks;
        }

        /* post non-blocking receive for message with tag equal to source rank plus one */
        uint64_t rtag = (uint64_t) src + 1;
        uint64_t rtagsel = 0xFFFFFFFFFFFFFFFF;
        psm_mq_req_t request;
        tmp_rc = psm_mq_irecv(mq, rtag, rtagsel, MQ_FLAGS_NONE, NULL, 0, NULL, &request);
        if (tmp_rc != PSM_OK) {
            rc = tmp_rc;
        }

        /* post blocking send to destination, set tag to be our rank plus one */
        uint64_t stag = (uint64_t) rank + 1;
        tmp_rc = psm_mq_send(mq, addrs[dst], MQ_FLAGS_NONE, stag, NULL, 0);
        if (tmp_rc != PSM_OK) {
            rc = tmp_rc;
        }

        /* wait on non-blocking receive to complete */
        tmp_rc = psm_mq_wait(&request, NULL);
        if (tmp_rc != PSM_OK) {
            rc = tmp_rc;
        }

        /* increase our distance by a factor of two */
        dist <<= 1;
    }

    return rc;
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
    psm_error_t *errors;
    uint64_t psm_ep_connect_timeout_nsecs;
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
    if(PSM_VERNO >= 0x0105)
    {
      for (j = 0; j < psmdev.np; j++) {
          if (allhostids[psmdev.me] == allhostids[j]) {
              if (j == psmdev.me) {
                  my_local_id = num_local_nodes;
              }
              num_local_nodes++;
          } 
      }

      snprintf(temp_str, sizeof(temp_str),"%d", num_local_nodes);
      setenv("MPI_LOCALNRANKS", temp_str, 0);
      snprintf(temp_str1, sizeof(temp_str1), "%d", my_local_id);
      setenv("MPI_LOCALRANKID", temp_str1, 0);
    }

    free(allhostids);

    /*Psm Layer has a bug in shared memory in Psm 2.0 version*/
    if(PSM_VERNO <= 0x0103)
      putenv("PSM_DEVICES=shm,ipath");

    psm_error_t psm_ret;
    if((psm_ret = psm_init(&verno_major, &verno_minor)) != PSM_OK)
    {
      error_abort_all(GEN_EXIT_ERR, "Failed to initialize PSM Device: %s", psm_error_get_string(psm_ret));
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
    if((psm_ret = psm_ep_open(uuid, NULL, &psmdev.ep, &my_epid)) != PSM_OK)
    {
        error_abort_all(GEN_EXIT_ERR, "Failed to open an end-point: %s", psm_error_get_string(psm_ret));
    }

    epid_list = (psm_epid_t *)malloc(sizeof(psm_epid_t)*psmdev.np);

    pmgr_allgather(&my_epid, sizeof(psm_epid_t), epid_list);

    psmdev.epaddrs = (psm_epaddr_t *) malloc(sizeof(psm_epaddr_t) * psmdev.np);
    errors = (psm_error_t *) malloc(sizeof(psm_error_t) * psmdev.np);

    if(psmdev.epaddrs == NULL || errors == NULL)
    {
        error_abort_all(GEN_EXIT_ERR, "Malloc error in init.\n");
    }

    psm_ep_connect_timeout_nsecs = viadev_psm_ep_connect_timeout_secs * 1e9;
    if((psm_ret = psm_ep_connect(psmdev.ep, psmdev.np, epid_list, NULL, errors,
                      psmdev.epaddrs, psm_ep_connect_timeout_nsecs)) != PSM_OK)
    {
        error_abort_all(GEN_EXIT_ERR, "Unable to connect the end points: %s", psm_error_get_string(psm_ret));
    }

    free(errors); 
    free(epid_list);

    if((psm_ret = psm_mq_init(psmdev.ep, PSM_MQ_ORDERMASK_ALL, NULL, 0,
                   &psmdev.mq)) != PSM_OK)
    {
        error_abort_all(GEN_EXIT_ERR, "Unable to initialize MQ: %s", psm_error_get_string(psm_ret));
    }

    /* execute barrier to ensure all tasks have returned from psm_ep_connect */
    if ((psm_ret = psm_mq_init_barrier(psmdev.mq, psmdev.me, psmdev.np, psmdev.epaddrs)) != PSM_OK) {
        error_abort_all(GEN_EXIT_ERR, "MQ barrier failed: %s", psm_error_get_string(psm_ret));
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

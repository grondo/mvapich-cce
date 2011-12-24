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



#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "mv_dev.h"
#include "mv_param.h"
#include "mv_priv.h"
#include "mv_buf.h"
#include "req.h"

#include "process/pmgr_collective_client.h"
#include "mpid_smpi.h"

#include "dreg.h"

#ifndef DISABLE_PTMALLOC
#include "mem_hooks.h"
#endif


void MV_Init_Connection(mvdev_connection_t * c);
void MV_Setup_Connections();


mvdev_connection_t *flowlist;

#ifdef _SMP_
int *hostnames_od;
extern char *cpu_mapping;
void viainit_setaffinity(char *);

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


/* TODO: REWRITE!!! */
void odu_init_SMP()
{
    int j;  
    int *smpi_ptr = NULL; 

    smpi.local_nodes = (unsigned int *) malloc(mvdev.np * sizeof(int));
    smpi_ptr = (int *) malloc(mvdev.np * sizeof(int));

    if (hostnames_od == NULL || smpi.local_nodes == NULL 
        || smpi_ptr == NULL) { 
        error_abort_all(GEN_EXIT_ERR, "malloc: for SMP");
    }

    smpi.only_one_device = 1;
    smpi.num_local_nodes = 0;
    for (j = 0; j < mvdev.np; j++) {
        if (hostnames_od[mvdev.me] == hostnames_od[j]) {
            if (j == mvdev.me) {
                smpi.my_local_id = smpi.num_local_nodes;
#ifdef _AFFINITY_
                if (viadev_enable_affinity)
                    viainit_setaffinity(cpu_mapping);
#endif                          /* _AFFINITY_ */
            }
            smpi.local_nodes[j] = smpi.num_local_nodes;
            smpi_ptr[smpi.num_local_nodes] = j;
            smpi.num_local_nodes++;
        } else {
            smpi.only_one_device = 0;
            smpi.local_nodes[j] = -1;
        }
    }
    smpi.l2g_rank =
        (unsigned int *) malloc(smpi.num_local_nodes * sizeof(int));
    if (smpi.l2g_rank == NULL) {
        error_abort_all(GEN_EXIT_ERR, "odu_init_SMP");
    }
    for (j = 0; j < smpi.num_local_nodes; j++) {
        smpi.l2g_rank[j] = smpi_ptr[j];
    }
    free(smpi_ptr);
}
#endif

int power_two(int x) {
    int pow = 1;
    while (x) { pow = pow * 2; x--; }
    return pow;
}

static inline int AUX_MTU_to_int(int mtu) {
    switch(mtu) {
        case IBV_MTU_256:
            return 256;
        case IBV_MTU_512:
            return 512;
        case IBV_MTU_1024:
            return 1024;
        case IBV_MTU_2048:
            return 2048;
        case IBV_MTU_4096:
            return 4096;
        default:
            return 2048;
    }
}

static void MV_Open_HCA(void)
{
    struct ibv_device **dev_list;
    int num_hcas;
    int i, j;
    int hca = 0;
    int ports_active = 0;

    dev_list = ibv_get_device_list(&num_hcas);

    if(!num_hcas) {
        error_abort_all(GEN_EXIT_ERR, "No HCAs found\n");
    }

    if(!(mvdev.hca = (mv_hca *) malloc(sizeof(mv_hca) * num_hcas))) {
        error_abort_all(GEN_EXIT_ERR, "Couldn't malloc\n");
    }

    mvdev.num_hcas    = num_hcas;
    /* TODO: support multiple HCAs */
    mvdev.num_hcas    = 1;

    mvdev.hca[hca].device = dev_list[hca];
    mvdev.default_hca = &(mvdev.hca[hca]);

    if(strncmp(MVDEV_INVALID_DEVICE, mvparams.device_name, 32)) {
        int found = 0;

        for(i = 0; i < num_hcas; i++) {
            mvdev.hca[hca].device = dev_list[i];
            mvdev.default_hca = &(mvdev.hca[hca]);

            if (!strncmp(ibv_get_device_name(dev_list[i]), 
                        mvparams.device_name, 32)) {
                found = 1;
                break;
            } else { 
                continue;
            }
        }
        if(!found) {
            error_abort_all(GEN_EXIT_ERR, "Couldn't find device %s\n", mvparams.device_name);
        }
    }


    if(!(mvdev.hca[hca].context = ibv_open_device(mvdev.hca[hca].device))) {
        error_abort_all(GEN_EXIT_ERR, "Error getting context\n");
    }

    if(ibv_query_device(mvdev.hca[hca].context, &mvdev.hca[hca].device_attr)) {
        error_abort_all(GEN_EXIT_ERR, "Error getting HCA attributes\n");
    }

    if(!(mvdev.hca[hca].pd = ibv_alloc_pd(mvdev.hca[hca].context))) {
        error_abort_all(GEN_EXIT_ERR, "Error getting pd\n");
    }

    /* allocate data structures for ports */
    mvdev.hca[hca].port_attr = (struct ibv_port_attr *)
        malloc(sizeof(struct ibv_port_attr) * 
                mvdev.hca[hca].device_attr.phys_port_cnt);

    for(j = 1; j <= mvdev.hca[hca].device_attr.phys_port_cnt; j++) {
        if(ibv_query_port(mvdev.hca[hca].context, j, &(mvdev.hca[hca].port_attr[j-1]))) {
            fprintf(stderr, "Failed to query port\n");
        }
    }

    for(j = 1; j <= mvdev.hca[hca].device_attr.phys_port_cnt; j++) {
        D_PRINT("HCA %d, port %d, state: %d\n",
                hca, j, mvdev.hca[hca].port_attr[j-1].state);
        if(mvdev.hca[hca].port_attr[j-1].state == IBV_PORT_ACTIVE) {
            mvdev.hca[hca].default_port_attr = &(mvdev.hca[hca].port_attr[j-1]);
            ports_active++;

            if(mvparams.use_lmc && &(mvdev.hca[hca]) == mvdev.default_hca) {
                mvparams.max_lmc_total = power_two(mvdev.hca[hca].port_attr[j-1].lmc);
            }
            if(&(mvdev.hca[hca]) == mvdev.default_hca) {
                mvparams.default_port = j;
                if(-1 == mvparams.mtu) {
                    mvparams.mtu = AUX_MTU_to_int(mvdev.hca[hca].port_attr[j-1].active_mtu);
                } else {
                    mvparams.mtu = MIN(mvparams.mtu, 
                            AUX_MTU_to_int(mvdev.hca[hca].port_attr[j-1].active_mtu));
                }
            }
            break;
        }
    }

    if(!ports_active) {
        error_abort_all(GEN_EXIT_ERR, "No active ports on HCA\n");
    }

    ibv_free_device_list(dev_list);
}

void AUX_Get_Hostname() 
{
    mvdev.my_name = (char *) malloc(sizeof(char) * HOSTNAME_LEN);
    if (gethostname(mvdev.my_name, sizeof(char) * HOSTNAME_LEN) < 0) {
        error_abort_all(GEN_EXIT_ERR, "gethostname failed");
    }
}

#ifdef _SMP_
static int AUX_Get_HostID(char * hostname) 
{
    int host_id = 0;
    struct hostent *hostent;

    hostent = gethostbyname(hostname);
    if (hostent == NULL) { 
        error_abort_all(GEN_EXIT_ERR,
                "Null value returned by gethostbyname");
    }

    host_id = ((struct in_addr *) hostent->h_addr_list[0])->s_addr;

    return host_id;
}
#endif

/* Exchange the lid and smp hostname information */
void AUX_Get_Remote_QPs() 
{
    typedef struct {
        pid_t   pid; 
        int hca_lid;
        int ud_qpn;
    } mv_exchange_info;

    mv_exchange_info my_info, *all_info;
    int i;

    my_info.pid     = getpid();
    my_info.hca_lid = mvdev.default_hca->default_port_attr->lid;
    my_info.ud_qpn  = mvdev.ud_qp[0].qp->qp_num;

    all_info = (mv_exchange_info *)  
        malloc(sizeof(mv_exchange_info) * mvdev.np);

    pmgr_allgather(&my_info, sizeof(mv_exchange_info), all_info);

    for(i = 0; i < mvdev.np; i++) {
        mvdev.lids[i] = all_info[i].hca_lid;
        mvdev.qpns[i] = all_info[i].ud_qpn;
        mvdev.pids[i] = all_info[i].pid;
    }   

#ifdef XRC
    /* TODO: move hostnames to be something passed into AUX_Get_Remote_QPs
     * -- XRC_Init shouldn't be here */
    mvdev.xrc_info = NULL;
    if(mvparams.xrc_enabled) {
        MV_XRC_Init(hostnames_od, mvdev.np, mvdev.global_id, mvdev.my_name);
    }
#endif


    free(all_info);
}


int MPID_MV_Init(int *argc, char ***argv, int *size, int *rank) 
{
    int i;
    char host[256], *buf;

    /* Get information about myself ... */
    if (PMGR_SUCCESS != pmgr_init(argc, argv, &mvdev.np, &mvdev.me,
                &mvdev.global_id)) {
        error_abort_all(GEN_EXIT_ERR, "pmgr_client_init failed");
    }   
    pmgr_open();

    mvdev.processes_buffer = NULL;
    mvdev.processes = NULL;
    gethostname(host, sizeof(host));
    pmgr_allgatherstr(host, &mvdev.processes, &mvdev.processes_buffer);

    *size = mvdev.np;
    *rank = mvdev.me;
    strcpy(mvdev.execname, ((const char **) (*argv))[0]);

    AUX_Get_Hostname();
    MV_Init_Params(mvdev.np, mvdev.me);

    mvdev.connections = (mvdev_connection_t *)
        malloc(sizeof(mvdev_connection_t) * mvdev.np);


#ifdef _SMP_
    int host_id = AUX_Get_HostID(mvdev.my_name);
    hostnames_od = (int *) malloc(mvdev.np * sizeof(int));
    pmgr_allgather(&host_id, sizeof(int), hostnames_od);

    if(!disable_shared_mem) {
        odu_init_SMP();
    }   
#endif

    /* need to figure out if reliability is being used */
    mvdev_header_size = MV_HEADER_NORM_OFFSET_SIZE;
   

    /* init the timers that allow us to determine ack timeouts */
    MV_Init_Timers();

#ifndef DISABLE_PTMALLOC
    /* Set Memory Hooks */
    if(mvapich_minit()) {
        fprintf(stderr, "Malloc seems to be overriden\n");
    }
#endif

    MV_Open_HCA();

    /* malloc the startup memory */

    mvdev.lids = (uint32_t *) malloc(sizeof(uint32_t) * mvdev.np);
    mvdev.qpns = (uint32_t *) malloc(sizeof(uint32_t) * mvdev.np);

    mvdev.connection_ack_required = (uint8_t *)
        malloc(sizeof(uint8_t) * mvdev.np);

    mvdev.pids = (int *) malloc(mvdev.np * sizeof(int));
    mvdev.pids[mvdev.me] = (int) getpid();
    mvdev.rpool_srq_list = NULL;

    mvdev.last_qp = 0;

    mvdev.rc_connections = 0;
    mvdev.rcfp_connections = 0;



    /* Intialize data structures for each connection 
     * This is before the QPs are created or information
     * is exchanged 
     */
    for(i = 0; i < mvdev.np; i++) {
        mvdev_connection_t *c = &(mvdev.connections[i]);
        c->global_rank = i;

        MV_Init_Connection(&(mvdev.connections[i]));
    }

    /* more general intialization */
    mvdev.unack_queue_head = mvdev.unack_queue_tail = NULL;

    mvdev.posted_bufs = 0;
    mvdev.total_volume = 0;
    mvdev.total_messages = 0;

    mvdev.polling_set = (mvdev_connection_t **) 
        malloc(sizeof(mvdev_connection_t *) * mvparams.max_rc_connections);
    mvdev.polling_set_size = 0;

    MV_Setup_Connections();

    pmgr_barrier();
    
    /* we require various threads for communication */
    MV_Start_Threads();

    /* intialize the cache for mrs */
    dreg_init();

    pmgr_close();

#ifdef _SMP_
    free(hostnames_od);
#endif

    return 0;
}

void MV_Setup_Connections() 
{

    /* Setup local connections */
    if(!MV_Setup_QPs()) {
        error_abort_all(GEN_EXIT_ERR, "MV_Setup_UD_QPs");
    }

    if(mvparams.use_zcopy) {
        MV_Setup_Rndv_QPs() ;
    }

    allocate_mv_bufs(6000);
    CHECK_RPOOL(mvdev.ud_qp[0].rpool);

    /* Get the remote qp information */
    AUX_Get_Remote_QPs(mvdev.connections);
    /* Now generate the address handles */
    MV_Generate_Address_Handles(mvdev.connections);
}

/* Initialize all of the attributes _required_ for the most
 * basic of communication to this host. Other intialization will
 * only occur when communication is initiated
 */

void MV_Init_Connection(mvdev_connection_t * c) 
{

    /* set a few general attributes that keep track of what
     * communication channels we have open, etc
     */


    c->inflow = 0;
    c->rhandle = NULL;
    c->shandle_head = NULL;
    c->shandle_tail = NULL;
    c->nextflow = NULL;
    c->rcfp_remote_credit = 0;
    c->rcrq_remote_credit = 0;

    /* we start at 1 and 0 is reserved for non-ordered messages */
    c->seqnum_next_tosend = 1;
    c->seqnum_next_toack = 1;

    c->queued = 0;

    c->recv_window_head = NULL;
    c->recv_window_tail = NULL;
    c->recv_window_size = 0;
    c->recv_window_start_seq = 1;

    c->send_window_head = NULL;
    c->send_window_tail = NULL;
    c->send_window_size = 0;
    c->send_window_segments = 0;

    c->ext_window_head = NULL;
    c->ext_window_tail = NULL;
    c->rc_channel_head = NULL;

    c->rc_largest = 0;
    c->alloc_srq = 0;
    c->tag_counter = 0;
    c->unrel_tags_head = NULL;
    
    c->pending_rcfp = NULL;

    /* TODO: only do this when using UD for this connection */
    c->r3_segments = 0;
    c->blocked_count = 0;

#ifdef XRC
    c->xrc_channel_ptr = NULL;
#endif


    c->data_ud_ah = (struct ibv_ah **) 
        malloc(sizeof(struct ibv_ah *) * mvparams.max_lmc_total * mvparams.max_sl);
    c->last_ah = 0;

    c->rc_enabled = 0;
    c->xrc_enabled = 0;
    c->rcfp_send_enabled = 0;
    c->rcfp_send_enabled_xrc_pending = 0;
    c->rcfp_recv_enabled = 0;

    c->channel_request_head = c->channel_request_tail = NULL;

    c->rcfp_messages = 0;
    c->channel_rc_fp = NULL;

    mvdev.connection_ack_required[c->global_rank] = 0;

#ifdef MV_PROFILE
    mvdev_init_connection_profile(c);
#endif
}

int vapi_proc_info(int i, char **host, char **exename)
{
    int pid;
    pid = mvdev.pids[i];
    *host = mvdev.processes[i];
    *exename = mvdev.execname;
    return (pid);
}

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



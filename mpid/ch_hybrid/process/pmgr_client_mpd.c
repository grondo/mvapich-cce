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

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <assert.h>

#include "../../mpd/bnr.h"
#include "../../mpd/mpdlib.h"
#include "pmgr_client.h"

/* The global ID for this job (must be unique) */
static BNR_Group pmgr_id;
extern int BNR_Get_group_id(BNR_Group group);

/* Total number of processes in the job */
static int pmgr_np;

/* My rank in the job */
static int pmgr_me;

/* Insure init only gets called once */
static int pmgr_initialized = 0;

static int *allpids = NULL;

static int mpd_exchange_info(void *localaddr, int addrlen, 
	void *alladdrs, int unlimited);

/* Find out how many processes in job and our rank */
int pmgr_client_init(int *argc_p, char ***argv_p,
                     int *np_p, int *me_p, int *id_p, 
		     char *** processes_p)
{
    /*
     * Make sure we only init once!
     */
    if (pmgr_initialized) {
        fprintf(stderr, "PMGR already initialized");
        return (0);
    }
    pmgr_initialized = 1;

    /* Push out stdio as soon as newline is seen */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /*
     * Init connection to MPD and get our global id
     */
    BNR_Init();
    BNR_Get_group(&pmgr_id);
    BNR_Get_size(pmgr_id, &pmgr_np);
    BNR_Get_rank(pmgr_id, &pmgr_me);
    *id_p = BNR_Get_group_id(pmgr_id);

    *np_p = pmgr_np;
    *me_p = pmgr_me;

    /* Also needs to exchange hostnames between processes */
    {
	int i, len;
	pid_t ppid;
	char *loc, *allnames;
	char **pmgr_processes;

	char hostname[256];
	char local_name[256];

	gethostname(hostname, 256);      

	loc = strchr(hostname, '.');
	if (loc) *loc = '\0';

	ppid = getpid();
	sprintf(local_name, "%35s:%08d:", hostname, ppid);

	len = strlen(local_name);
	allnames = (char *) malloc (pmgr_np * len + 4);
	bzero(allnames, pmgr_np * len + 4);

	mpd_exchange_info(local_name, len, allnames, 1);

	pmgr_processes = calloc(pmgr_np, sizeof(char*));
	allpids        = calloc(pmgr_np, sizeof(int));

	/* Cannot use strtok function :-( */
	for (i = 0; i < pmgr_np; i++) {

	    char temp[32];
	    int  pid, num_tokens;
	    char *ptr;

	    loc = strchr(allnames, ':');
	    ptr = (char *)loc+1;
	    if(loc) *loc= '\0';

	    strncpy(temp, allnames, strlen(allnames)+1);
	    pmgr_processes[i] = strdup(temp);

	    /* remove white space */
	    loc = strchr(pmgr_processes[i], ' ');
	    if(loc) *loc='\0';

	    /* Parse the pid */
	    num_tokens = sscanf(ptr, "%08d:", &pid);
	    assert (num_tokens==1);
	    allpids[i] = pid;

	    allnames = allnames + len;
	}
	*processes_p = pmgr_processes;
    }
    return (1);
}

#ifdef MCST_SUPPORT
int pmgr_exchange_mcs_group_sync(void *grp_info,
                                 int grp_info_len, int root)
{
    int len_remote;
    char attr_buff[BNR_MAXATTRLEN];
    char val_buff[BNR_MAXVALLEN];

    /* root sends out mcs group information */
    if (root != 0 || grp_info_len != GRP_INFO_LEN) {
        fprintf(stderr, "root must be 0 for "
                "pmgr_exchange_mcs_group_sync\n");
        fprintf(stderr, "grp_info_len must be %d\n", GRP_INFO_LEN);
        sleep(1);
        exit(1);
    }

    /* root 0 put a message to the repository, 
       all others extract from the repository */
    /* Be sure to use different keys for different processes */
    if (0 == pmgr_me) {
	bzero(attr_buff, BNR_MAXATTRLEN * sizeof(char));
	bzero(val_buff, BNR_MAXVALLEN * sizeof(char));
	snprintf(attr_buff, BNR_MAXATTRLEN, "MCST_%04d\n", pmgr_me);
	snprintf(val_buff, BNR_MAXVALLEN, "%s\n", grp_info);
	val_buff[grp_info_len] = '\0';

	/* Put my key and val to mpdman */
	BNR_Put(pmgr_id, attr_buff, val_buff, -1);
    }

    /* Barrier */
    BNR_Fence(pmgr_id);

    if (0 != pmgr_me) {
	/* Use the key to extract the value */
	bzero(attr_buff, BNR_MAXATTRLEN * sizeof(char));
	bzero(val_buff, BNR_MAXVALLEN * sizeof(char));
	snprintf(attr_buff, BNR_MAXATTRLEN, "MCST_%04d\n", 0);
	BNR_Get(pmgr_id, attr_buff, val_buff);

	/* Simple sanity check before stashing it to the alladdrs */
	len_remote = strlen(val_buff);
	/*assert(len_remote == grp_info_len);*/

	/* Copy value into grp_info */
	memcpy(grp_info, val_buff, len_remote);
    }

    /* Barrier to finish MCST synchronization */
    BNR_Fence(pmgr_id);
    return 1;
}
#endif

/* Exchange address info with other processes in the job.
 * MPD provides the ability for processes within the job to
 * publish information which can then be querried by other
 * processes.  It also provides a simple barrier sync.
 */
int pmgr_exchange_addresses(void *localaddr, int addrlen, 
	void *alladdrs, void * pallpids)
{
    mpd_exchange_info(localaddr, addrlen, alladdrs, 0);
    /* XXX, assumed pid_t is an integer */
    memcpy(pallpids, allpids, pmgr_np * sizeof(int));
    free(allpids);
    return 1;
}


static int mpd_exchange_info(void *localaddr, int addrlen, 
	void *alladdrs, int unlimited)
{
    int i, j, lhs, rhs, len_local, len_remote;
    char attr_buff[BNR_MAXATTRLEN];
    char val_buff[BNR_MAXVALLEN];
    char *temp_localaddr = (char *) localaddr;
    char *temp_alladdrs = (char *) alladdrs;

    len_local = strlen(temp_localaddr);

    /* Be sure to use different keys for different processes */
    bzero(attr_buff, BNR_MAXATTRLEN * sizeof(char));
    snprintf(attr_buff, BNR_MAXATTRLEN, "MVAPICH_%04d\n", pmgr_me);

    /* Put my key and val to mpdman */
    i = BNR_Put(pmgr_id, attr_buff, temp_localaddr, -1);

    /* Wait until processes done the same */
    i = BNR_Fence(pmgr_id);

    lhs = (pmgr_me + pmgr_np - 1) % pmgr_np;
    rhs = (pmgr_me + 1) % pmgr_np;

    if (unlimited) {
	for (j = 0; j < pmgr_np; j++) {
	    if (j == pmgr_me) {
		/* Insert my own address into alladdrs for compatibility. */
		strncpy(temp_alladdrs, localaddr, len_local);
		temp_alladdrs += len_local;
	    } else {
		/* Use the key to extract the value */
		bzero(attr_buff, BNR_MAXATTRLEN * sizeof(char));
		bzero(val_buff, BNR_MAXVALLEN * sizeof(char));
		snprintf(attr_buff, BNR_MAXATTRLEN, "MVAPICH_%04d\n", j);
		BNR_Get(pmgr_id, attr_buff, val_buff);

		/* Simple sanity check before stashing it to the alladdrs */
		len_remote = strlen(val_buff);

		assert(len_remote == len_local);

		strncpy(temp_alladdrs, val_buff, len_remote);
		temp_alladdrs += len_remote;
	    }
	}

    i = BNR_Fence(pmgr_id);

	return 1;
    }

#ifdef USE_MPD_BASIC
    for (j = 0; j < pmgr_np; j++) {
        if (j == pmgr_me) {
            /* Insert my own address into alladdrs for compatibility. */
            strncpy(temp_alladdrs, localaddr, len_local);
            temp_alladdrs += len_local;
        } else                  /* and all the others */
#else
    for (i = 0; i < 2; i++) {

        /* get lhs and rhs processes' data */
        j = (i == 0) ? lhs : rhs;
#endif
        {
            /* Use the key to extract the value */
            bzero(attr_buff, BNR_MAXATTRLEN * sizeof(char));
            bzero(val_buff, BNR_MAXVALLEN * sizeof(char));
            snprintf(attr_buff, BNR_MAXATTRLEN, "MVAPICH_%04d\n", j);
            BNR_Get(pmgr_id, attr_buff, val_buff);

            /* Simple sanity check before stashing it to the alladdrs */
            len_remote = strlen(val_buff);
            assert(len_remote == len_local);

            strncpy(temp_alladdrs, val_buff, len_remote);
            temp_alladdrs += len_remote;
        }
    }

    return (1);
}

/* we can use the socket network established during initialization
 * to implement a simple barrier.  All processes write a message
 * on the socket back to the launching process, when wait for
 * a reply.  The launching process waits until all messages 
 * have arrived then writes back to all the processes.
 *
 * The VIA network is faster, so the default here is to return
 * failure (0) and have the barrier go over the VI network.
 */

int pmgr_barrier()
{
    BNR_Fence(pmgr_id);
    return (1);
}

/* abort call to process manager.  Allows it to clean-up
 * any resources it might have allocated.
 */
int pmgr_abort(int none)
{
    MPD_Abort(1);
    return(1);
}

/* final call to process manager.  Allows it to clean-up
 * any resources it might have allocated for us.
 */
int pmgr_finalize()
{
#ifdef MPICH_1_2_5
    /* Not implemented yet
       BNR_Finalize();
     */
#endif
    return (1);
}

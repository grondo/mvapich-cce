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
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include "pmgr_client.h"

static char *mpirun_hostname;
struct hostent *mpirun_hostent;

static int mpirun_port;

static int pmgr_me, pmgr_nprocs, pmgr_id;
static int mpirun_socket;

void pmgr_read_int(int * buff, int sd);
void pmgr_read_gen_type(void * buff,size_t len,int sd);

int pmgr_client_init(int *argc_p, char ***argv_p, int *np_p, int *me_p,
                     int *id_p, char ***processes_p)
{
    char *str;
    char *str_token;
    char **pmgr_processes = NULL;
    int i;
    setvbuf(stdout, NULL, _IONBF, 0);
    char *value;

    /* Get information from environment, not from the argument list */

    /* mpirun host */
    str = getenv("MPIRUN_HOST");
    if (str == NULL) {
        fprintf(stderr, "Can't read MPIRUN_HOST\n");
        exit(1);
    }
    mpirun_hostname = strdup(str);
    mpirun_hostent = gethostbyname(mpirun_hostname);
    if (!mpirun_hostent) {
        fprintf(stderr,"gethostbyname failed:: %s: %s (%d)\n",
               mpirun_hostname, hstrerror(h_errno), h_errno);
        exit(1);
    }   

    /* mpirun port */
    str = getenv("MPIRUN_PORT");
    if (str == NULL) {
        fprintf(stderr, "Can't read MPIRUN_PORT\n");
        exit(1);
    }
    mpirun_port = atoi(str);
    if (mpirun_port <= 0) {
        fprintf(stderr, "Invalid MPIRUN port %s\n", str);
        exit(1);
    }

    /* number of processes  */
    str = getenv("MPIRUN_NPROCS");
    if (str == NULL) {
        fprintf(stderr, "Can't read MPIRUN_NPROCS\n");
        exit(1);
    }
    pmgr_nprocs = atoi(str);
    if (pmgr_nprocs <= 0) {
        fprintf(stderr, "Invalid MPIRUN nprocs %s\n", str);
        exit(1);
    }

    /* rank of current process */
    str = getenv("MPIRUN_RANK");
    if (str == NULL) {
        fprintf(stderr, "Can't read MPIRUN_RANK\n");
        exit(1);
    }
    pmgr_me = atoi(str);
    if (pmgr_me < 0 || pmgr_me >= pmgr_nprocs) {
        fprintf(stderr, "Invalid MPIRUN rank %s\n", str);
        exit(1);
    }

    /* unique of current application */
    str = getenv("MPIRUN_ID");
    if (str == NULL) {
        fprintf(stderr, "Can't read MPIRUN_ID\n");
        exit(1);
    }
    pmgr_id = atoi(str);
    if (pmgr_id == 0) {
        fprintf(stderr, "Invalid application ID %s\n", str);
        exit(1);
    }

   /* list of hostnames running processes in job */
    if ((value = getenv("NOT_USE_TOTALVIEW")) == NULL) {
        pmgr_processes = (char **) calloc((size_t)pmgr_nprocs, sizeof(char*));
        if (pmgr_processes == NULL) {
            fprintf(stderr, "Can't allocate process list\n");
            exit(1);
        }
        str = getenv("MPIRUN_PROCESSES");
        if (str == NULL) {
            fprintf(stderr, "Can't read MPIRUN_PROCESSES\n");
            exit(1);
        }
        str = strdup(str);
        if (str == NULL) {
            fprintf(stderr, "Can't allocate process list\n");
            exit(1);
        }
        for (i = 0; i < pmgr_nprocs; i++) {
            if (!str) {
                fprintf(stderr, "Invalid MPIRUN process list: '%s' ",
                        getenv("MPIRUN_PROCESSES"));
                exit(1);
            }
            str_token = strchr(str, ':');
            if(str_token)
                *str_token = ' ';
            pmgr_processes[i] = str;
            str = strchr(str, ' ');
            if (str) {
                *str = '\0';
                str++;
            }
        }
    }
#if 0
    fprintf(stderr, "Process %d of %d in application %d checking in\n", 
	    pmgr_me, pmgr_nprocs, pmgr_id);
    fprintf(stderr, "Will connect to mpirun at host %s port %d\n", 
	    mpirun_hostname, mpirun_port);
#endif
 
    *np_p = pmgr_nprocs;
    *me_p = pmgr_me;
    *id_p = pmgr_id;
    *processes_p = pmgr_processes;

   return 1;
}

void pmgr_init_connection(int phase)
{
    int nwritten;
    int version;
    int connect_attempt = 0, max_connect_attempts = 5;
    struct sockaddr_in sockaddr;

    if(phase != 0) return;

   /*
     * Exchange information with the mpirun program. Send it our
     * socket address, get back addresses for our siblings.
     */

    mpirun_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (mpirun_socket < 0) {
        perror("socket");
        exit(1);
    }

    mpirun_hostent = gethostbyname(mpirun_hostname);
    if (mpirun_hostent == NULL) {
        herror("gethostbyname");
        exit(1);
    }

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr = *(struct in_addr *) (*mpirun_hostent->h_addr_list);
    sockaddr.sin_port = htons(mpirun_port);

    srand(pmgr_me);

    while(connect(mpirun_socket, (struct sockaddr *) &sockaddr,
		sizeof(sockaddr)) < 0) {
	if(connect_attempt++ < max_connect_attempts) {
	    sleep(1 + (int) (10.0 * (rand() / (RAND_MAX + 1.0))));
	}

	else {
	    perror("connect");
	    exit(1);
	}
    }

    /* we are now connected to the mpirun program */

    /* 
     * Exchange information with mpirun.  If you make any changes
     * to this protocol, be sure to increment the version number
     * in the header file.  This is to permit compatibility with older
     * executables.
     */

    version = PMGR_VERSION;
    /* first, send a version number */
    nwritten = write(mpirun_socket, &version, sizeof(version));
    if (nwritten != sizeof(version)) {
	sleep(2);
	perror("write");
	exit(1);
    }
    
    /* next, send our rank */
    nwritten = write(mpirun_socket, &pmgr_me, sizeof(pmgr_me));
    if (nwritten != sizeof(pmgr_me)) {
        sleep(2);
        perror("write");
        exit(1);
    }
}


int pmgr_exchange_hca_and_hostid (int hostid, int hostidlen, 
        int *allhostids, int hca_type , int *is_homogeneous)

{
    int tot_nread = 0;
    int toread_len = 0;
    int nread, nwritten;
    int retries, retry_max = 10;
  
    pmgr_init_connection(0);
    /* next, send size of addr */
    nwritten = write(mpirun_socket, &hostidlen, sizeof(hostidlen));
    if (nwritten != sizeof(hostidlen)) {
        sleep(2);
        perror("write");
        exit(1);
    }
    /* next, send our hostid */
    nwritten = write(mpirun_socket, &hostid, hostidlen);
    if (nwritten != hostidlen) {
        perror("write");
        sleep(2);
        exit(1);
    }


    /* next, send  own hca_type */
    nwritten = write(mpirun_socket, &hca_type, sizeof(hca_type));
    if (nwritten != sizeof(hca_type)) {
        sleep(2);
        perror("write");
        exit(1);
    }

    /* next, read if cluster is homogeneous */
    pmgr_read_int (is_homogeneous,mpirun_socket);

    
    toread_len =  pmgr_nprocs * sizeof(int);
    /* finally, read addresses from all processes */
    for(retries = 0; tot_nread < toread_len && retries < retry_max; retries++) {
        nread =
            read(mpirun_socket, ((void *)allhostids) + tot_nread,
                    toread_len - tot_nread);
        if (nread < 0) {
            perror("read");
            sleep(2);
            exit(1);
        }
        tot_nread = tot_nread + nread;
    }

    if(tot_nread != toread_len) {
	fprintf(stderr, "Unable to read from mpirun_socket after %d tries!\n",
		retry_max);
	exit(EXIT_FAILURE);
    }

    fflush(stdout);
    return 1;
}

int pmgr_exchange_addresses(void *localaddr, int addrlen, void *alladdrs,
	void * pallpids)
{
    int i, tot_nread = 0;
    int toread_len = 0;
    int nread, nwritten;
    int retries, retry_max = 10;

    pid_t my_pid_int = getpid();
    int pidlen, mypid_len; 
    pid_t *ppids = (pid_t *)pallpids;
    pid_t *allpids = NULL;

    /* next, send size of addr */
    nwritten = write(mpirun_socket, &addrlen, sizeof(addrlen));
    if (nwritten != sizeof(addrlen)) {
        sleep(2);
        perror("write");
        exit(1);
    }

#ifdef DEBUG
    fprintf(stderr, "client: ---addrlen %d (", addrlen);
    fprintf(stderr, "[%d] send local addr info\n", pmgr_me);
    for (i = 0; i < addrlen / sizeof(int); i++) {
        fprintf(stderr, "%d\t", ((int *) localaddr)[i]);
    }
    fprintf(stderr, "\n");
#endif

    /* next, send our address */
    nwritten = write(mpirun_socket, localaddr, (size_t) addrlen);
    if (nwritten != addrlen) {
        perror("write");
        exit(1);
    }
 
    mypid_len = sizeof(my_pid_int); 
  
    pidlen=mypid_len;	
    allpids = (pid_t *)malloc((size_t) mypid_len*pmgr_nprocs);
    if (allpids == NULL) {
  	 fprintf(stderr, "melloc error for allpids\n");
       exit(1);
    }

    /* next, send size of pid */
    nwritten = write(mpirun_socket, &pidlen, sizeof(pidlen));
    if (nwritten != sizeof(mypid_len)) {
      sleep(2);
      perror("write");
      exit(1);
    }

    /* next, send our pid */
    if (pidlen != 0) {
      nwritten = write(mpirun_socket, &my_pid_int, (size_t) pidlen);
      if (nwritten != pidlen) {
        perror("write");
        exit(1);
      }
    }
    
    toread_len = 3 * pmgr_nprocs * sizeof(int);
    /* finally, read addresses from all processes */
    for(retries = 0; tot_nread < toread_len && retries < retry_max; retries++) {
	nread =
	    read(mpirun_socket, (void *) ((char *) alladdrs + tot_nread),
		 (size_t) (toread_len - tot_nread));
	if (nread < 0) {
	    perror("read");
	    exit(1);
	}
	tot_nread = tot_nread + nread;
    }

    if(tot_nread != toread_len) {
	fprintf(stderr, "Unable to read from mpirun_socket after %d tries!\n",
		retry_max);
	exit(EXIT_FAILURE);
    }

    if (pidlen != 0) {
	tot_nread=0;
	/* finally, read pids from all processes */
	for(retries = 0; tot_nread < pmgr_nprocs*pidlen && retries < retry_max;
		retries++) {
	    nread = read(mpirun_socket, (void*)((char *)allpids+tot_nread), 
		    (size_t) ((pmgr_nprocs*pidlen)-tot_nread));
	    if (nread < 0) {
		perror("read");
		sleep(2);
		exit(1);
	    }
	    tot_nread = tot_nread + nread;
	}

	if(tot_nread != pmgr_nprocs*pidlen) {
	    fprintf(stderr, "Unable to read from mpirun_socket after %d "
		    "tries!\n", retry_max);
	    exit(EXIT_FAILURE);
	}
    }

    fflush(stdout);

#ifdef USE_VIADEV_BARRIER
    close(mpirun_socket);
#endif

    if (allpids) {		
        for(i=0;i < pmgr_nprocs; i++) {
            ppids[i] = allpids[i];
        } 
        free(allpids);	
    }	
 
    return 1;
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
#ifdef USE_VIADEV_BARRIER
    return (0);
#else
    /* this function gets called during termination, after the
     * async error handler is cancelled.
     * We first write to our parent, then wait on a read for
     * a response.  This is a crude implementation of a barrier
     * but once the read completes, we know all other processes
     * have cancelled their error handlers and we can proceed to
     * terminate.
     */

    int send_val = pmgr_me;
    int recv_val = -1;
    int nread, nwritten;

    nwritten = write(mpirun_socket, &send_val, sizeof(send_val));
    if (nwritten != sizeof(send_val)) {
        perror("termination write");
        exit(1);
    }
    /* printf("[%d]send_val=%d\n", pmgr_me, send_val); */

    nread = read(mpirun_socket, &recv_val, sizeof(recv_val));
    if (nread != sizeof(recv_val)) {
        perror("termination read");
        sleep(1);
        exit(1);
    }

    /* printf("[%d]recv_val=%d\n", pmgr_me, recv_val); */

#ifndef MCST_SUPPORT
    close(mpirun_socket);
#endif

    /* return 1 to indicate we completed a barrier */
    return (1);
#endif
}

/* No cleanup necessary here.
 */
int pmgr_finalize()
{
    return (1);
}

/*
 * Call into the process spawner, using the same port we were given
 * at startup time, to tell it to abort the entire job.
 */
int pmgr_abort(int flag)
{
    int s;
    struct sockaddr_in sin;
    struct hostent *he;
    char*  str;

    he = gethostbyname(mpirun_hostname);
    if (!he) {
        fprintf(stderr,"gethostbyname failed:: %s: %s (%d)\n",
               mpirun_hostname, hstrerror(h_errno), h_errno);
        return -1;
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = he->h_addrtype;
    memcpy(&sin.sin_addr, he->h_addr_list[0], sizeof(sin.sin_addr));
    sin.sin_port = htons(mpirun_port);
    if (connect(s, (struct sockaddr *) &sin, sizeof(sin)) < 0)
        return -1;

        /* write our rank to mpirun_rsh (wait_for_error)
                 * for use in nice error messages 
            */
    write(s, &flag, sizeof(flag));
    write(s, &pmgr_me, sizeof(pmgr_me));

    close(s);
    return 0;
}


void pmgr_read_gen_type(void * buff,size_t len,int sd) {
    fd_set mask;
    int res, n = 0;
    struct timeval timeout;
    int try_counter = 20;

    while (n < len) {
        n += (res = read(sd,buff+n,len-n));
        if (res < 0) {
            if ((errno == EINTR) || (errno == EAGAIN) ) {
                n-=res;
                if (try_counter-- < 0) {
                    fprintf(stderr,"<pmgr_read_gen_type> "
                            "Can't read from socket %d, exiting\n",sd);
                    exit(1);
                }
            }
            else
            {
                perror("read");
            }
        }
    }

}

void pmgr_read_int(int * buff, int sd) {
    pmgr_read_gen_type((void*) buff, sizeof(int),sd);
}


#ifdef MCST_SUPPORT

int pmgr_exchange_mcs_group_sync(void *grp_info,
                                 int grp_info_len, int root)
{
    int send_val = pmgr_me;
    int recv_val = -1;
    int nread, nwritten;

    /* root sends out mcs group information */
    if (root != 0 || grp_info_len != GRP_INFO_LEN) {
        fprintf(stderr, "root must be 0 for "
                "pmgr_exchange_mcs_group_sync\n");
        fprintf(stderr, "grp_info_len must be %d\n", GRP_INFO_LEN);
        sleep(1);
        exit(1);
    }
    if (pmgr_me == root) {
        nwritten = write(mpirun_socket, grp_info, grp_info_len);
        if (nwritten != grp_info_len) {
            perror("pmgr_exchange_mcs_group_sync: root write");
            sleep(1);
            exit(1);
        }

        nread = read(mpirun_socket, &recv_val, sizeof(recv_val));
        if (nread != sizeof(recv_val)) {
            perror("pmgr_exchange_mcs_group_sync: root read");
            sleep(1);
            exit(1);
        }
    }

    /* read mcs group information and send an ack backs. */
    if (pmgr_me != root) {
        nread = read(mpirun_socket, grp_info, grp_info_len);
        if (nread != grp_info_len) {
            fprintf(stderr, "[%d]pmgr_exchange_mcs_group_sync: read grp "
                    "info %d(expected:%d)\n", pmgr_me, nread,
                    grp_info_len);
            sleep(1);
            exit(1);
        }

        nwritten = write(mpirun_socket, &send_val, sizeof(send_val));
        if (nwritten != sizeof(send_val)) {
            perror("pmgr_exchange_mcs_group_sync: non root"
                   " mcs grp sync write");
            sleep(1);
            exit(1);
        }
    }

    close(mpirun_socket);

    return 1;
}
#endif

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

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>

#include "mpirun_util.h"
#include "mpispawn_tree.h"
#include "pmgr_collective_mpirun.h"

extern process_info_t *local_processes;
extern int mpirun_socket;
int NCHILD;
int NCHILD_INCL = 1;
int N;
int *mpispawn_fds;
int MPISPAWN_NCHILD;

static inline int env2int(char * env_ptr) {
    return (env_ptr = getenv(env_ptr)) ? atoi(env_ptr) : 0;
}

int MPISPAWN_HAS_PARENT;
#define MPISPAWN_PARENT_FD mpispawn_fds[0]
#define MPISPAWN_CHILD_FDS (&mpispawn_fds[MPISPAWN_HAS_PARENT])

extern child_t *children;

int ROOT_FD = -1;

#define mtpmgr_debug(x, ...)
void mtpmgr_error(char *fmt, ...)
{
    va_list argp;
    fprintf(stderr, "PMGR_COLLECTIVE ERROR: ");
    va_start(argp, fmt);
    vfprintf(stderr, fmt, argp);
    va_end(argp);
    fprintf(stderr, "\n");
    mpispawn_abort (MPISPAWN_PMGR_ERROR);
}

int mtpmgr_write_fd (int sock, void *buf, int size) 
{
    int rc;
    int n = 0;
    char *offset = (char *) buf;
    
    while (n < size) {
        rc = write (sock, offset, size - n);

        if (rc < 0) {
	        if(errno == EINTR || errno == EAGAIN) continue;
	        return rc;
        }
        else if (0 == rc)
            return n;

        offset += rc;
        n += rc;
    }

    return n;
}

int mtpmgr_read_fd (int sock, void *buf, int size) 
{
    int rc;
    int n = 0;
    char *offset = (char *) buf;
#ifdef MT_TEST 
    if (env2int ("MPISPAWN_ID") == 2) {
        fprintf (stderr, "Dummy error\n");
        return 0;
    }
#endif

#ifdef MT_TEST 
    if (env2int ("MPISPAWN_ID") == (rand () % 1024)) {
        fprintf (stderr, "Dummy error\n");
        return 0;
    }
#endif 

    while (n < size) {
        rc = read (sock, offset, size - n);
       
        if (rc < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return rc;
        }
        else if (0 == rc) 
            return n;
    
        offset += rc;
        n += rc;
    }

    return n;
}

void mtpmgr_send (void *buf, int size, int sock)
{
	if (mtpmgr_write_fd(sock, buf, size) < 0) {
		mtpmgr_error("writing to (write() %m errno=%d) @ file %s:%d",
			errno, __FILE__, __LINE__);
	}
}

void mtpmgr_recv (void *buf, int size, int sock)
{
    if (mtpmgr_read_fd(sock, buf, size) <= 0) {
		mtpmgr_error("reading from (read() %m errno=%d) @ file %s:%d",
			errno, __FILE__, __LINE__);
    }
}

int mtpmgr_recv_int (int sock) 
{
    int buf = 0;
    mtpmgr_recv (&buf, sizeof (buf), sock);
    return buf;
}

int mtpmgr_set_current(int curr, int new)
{
	if (curr == -1) curr = new;
	if (new != curr) 
		mtpmgr_error("unexpected value: received %d, expecting %d @ file "
                "%s:%d", new, curr, __FILE__, __LINE__);
	return curr;
}

void mtpmgr_bcast_children (void *buf, int size) 
{
    int i;

    for (i = 0; i < NCHILD; i ++)
        mtpmgr_send (buf, size, children[i].fd);
}

void mtpmgr_bcast_subtree (void *buf, int size) 
{
    int i;
    for (i = 0; i < MPISPAWN_NCHILD; i++) {
        mtpmgr_send (buf, size, MPISPAWN_CHILD_FDS[i]);
    }

}

void mtpmgr_alltoallbcast (void *buf, int size)
{
    int pbufsize = size * N;
    void *pbuf = NULL;
    int i, src;
    pbuf = (void *) malloc (pbufsize);
    if (!pbuf) {
        mtpmgr_error ("PMGR MPISPAWN: Insufficient memory");
    }

    for (i = 0; i < NCHILD; i++) {
        for (src = 0; src < N; src++) {
            memcpy (pbuf + size * src, buf + size * (src*N + children[i].rank),
                    size);
        }
        mtpmgr_send (pbuf, pbufsize, children[i].fd);
    }
    free (pbuf);
}

int mt_id;

int mtpmgr_init (void)
{
    int i, nchild_subtree = 0, tmp;
    int *children_subtree = (int *) malloc (sizeof (int) * MPISPAWN_NCHILD);
    mt_id = env2int ("MPISPAWN_ID");
    if (MPISPAWN_NCHILD) {
        for (i = 0; i < MPISPAWN_NCHILD; i++) {
            tmp = mtpmgr_recv_int(MPISPAWN_CHILD_FDS[i]);
            nchild_subtree += tmp;
            children_subtree[i] = tmp;
        }
    }
    NCHILD_INCL = nchild_subtree;
    nchild_subtree += NCHILD;
    if (MPISPAWN_HAS_PARENT) {
        mtpmgr_send (&nchild_subtree, sizeof (int), MPISPAWN_PARENT_FD);
    }
    
    if (env2int ("MPISPAWN_USE_TOTALVIEW") == 1) {
        process_info_t *all_pinfo;
        int iter = 0;
        if (MPISPAWN_NCHILD) {
            all_pinfo = (process_info_t *) malloc 
                    (process_info_s * nchild_subtree);
            if (!all_pinfo) {
                mpispawn_abort (MPISPAWN_MEM_ERROR);
            }
            /* Read pid table from child MPISPAWNs */
            for (i = 0; i < MPISPAWN_NCHILD; i++) {
                read_socket (MPISPAWN_CHILD_FDS[i], &all_pinfo[iter], 
                        children_subtree [i] * process_info_s);
                iter += children_subtree [i];
            }
            for (i = 0; i < NCHILD; i++, iter++) {
                all_pinfo[iter].rank = local_processes[i].rank;
                all_pinfo[iter].pid = local_processes[i].pid;
            }
        }
        else {
            all_pinfo = local_processes;
        }
        if (MPISPAWN_HAS_PARENT) {
            write_socket (MPISPAWN_PARENT_FD, all_pinfo, 
                    nchild_subtree * process_info_s);
        } 
        else if (mt_id == 0) {
            /* Send to mpirun_rsh */
            write_socket (mpirun_socket, all_pinfo, 
                    nchild_subtree * process_info_s);
            /* Wait for Totalview to be ready */
            read_socket (mpirun_socket, &tmp, sizeof (int));
            close (mpirun_socket);
        }
        /* Barrier */
        if (MPISPAWN_HAS_PARENT) {
            read_socket (MPISPAWN_PARENT_FD, &tmp, sizeof (int));
        }
        if (MPISPAWN_NCHILD) {
            for (i = 0; i < MPISPAWN_NCHILD; i++) {
                write_socket (MPISPAWN_CHILD_FDS[i], &tmp, sizeof (int));
            }

        }
    }
    return 0;
}

int mtpmgr_processops (void)
{
    int quit = 0;
    while (!quit) {
        int opcode = -1;
        int root = -1;
        int size = -1;
        void *buf = NULL;
        void *send_buf = NULL;
        /* process ops from children */
        int i;
        for (i = 0; i < NCHILD; i++) {
            opcode = mtpmgr_set_current (opcode, mtpmgr_recv_int(children[i].fd));
            int rank, code;
            switch (opcode) {
                case PMGR_OPEN:
                    rank = mtpmgr_recv_int (children[i].fd);
                    MT_ASSERT (rank == children[i].rank);
                    break;
                case PMGR_CLOSE:
                    close (children[i].fd);
                    break;
                case PMGR_ABORT:
                    code = mtpmgr_recv_int (children[i].fd);
                    mtpmgr_error ("PMGR: Abort with code %d from rank %d", 
                            code, children[i].rank);
                    break;
                case PMGR_BARRIER:
                    break;
                case PMGR_BCAST:
                    root = mtpmgr_set_current (root, 
                            mtpmgr_recv_int(children[i].fd));
                    size = mtpmgr_set_current (size, 
                            mtpmgr_recv_int(children[i].fd));
                    if (!buf) buf = (void *) malloc (size);
                    if (children[i].rank == root) mtpmgr_recv (buf, size, 
                            children[i].fd);
                    break;
                case PMGR_GATHER:
                    root = mtpmgr_set_current (root, 
                            mtpmgr_recv_int(children[i].fd));
                    size = mtpmgr_set_current (size, 
                            mtpmgr_recv_int(children[i].fd));
#define SIZE (size + sizeof (int))
                    if (!buf) 
                        buf = (void *) malloc (SIZE * (NCHILD + NCHILD_INCL));
                    memcpy (buf + SIZE * i, &(children[i].rank), sizeof (int));
                    mtpmgr_recv (buf + SIZE * i + sizeof (int), size, 
                            children[i].fd);
#undef SIZE
                    break;
                case PMGR_SCATTER:
				    root = mtpmgr_set_current(root, 
                            mtpmgr_recv_int(children[i].fd));
				    size = mtpmgr_set_current(size, 
                            mtpmgr_recv_int(children[i].fd));
                    if (!buf) buf = (void *) malloc (size * N);
                    if (children[i].rank == root) 
                        mtpmgr_recv (buf, size * N, children[i].fd);
                    break;
                case PMGR_ALLGATHER:
				    size = mtpmgr_set_current(size, 
                            mtpmgr_recv_int(children[i].fd));
#define SIZE (size + sizeof (int))
				    if (!buf) 
                        buf = (void*) malloc(SIZE * (NCHILD + NCHILD_INCL));
                    memcpy (buf + SIZE * i, &children[i].rank, sizeof (int));
				    mtpmgr_recv(buf + SIZE * i + sizeof (int), size, 
                            children[i].fd);
#undef SIZE
                    break;
                case PMGR_ALLTOALL:
				    size = mtpmgr_set_current(size, 
                            mtpmgr_recv_int(children[i].fd));
#define SIZE (size * N + sizeof (int))
				    if (!buf) 
                        buf = (void*) malloc(SIZE * (NCHILD + NCHILD_INCL));
                    memcpy (buf + SIZE * i, &children[i].rank, sizeof (int));
				    mtpmgr_recv(buf + SIZE * i + sizeof (int), size * N, 
                            children[i].fd);
#undef SIZE
                    break;
                default:
                    mtpmgr_error ("Unrecognised PMGR opcode: %d from rank %d\n",
                            opcode, children[i].rank);
            }
        }

        /* Ensure that all my mpispawn children are in sync */
        if (MPISPAWN_NCHILD) {
            for (i = 0; i < MPISPAWN_NCHILD; i++) {
                opcode = mtpmgr_set_current (opcode, 
                        mtpmgr_recv_int(MPISPAWN_CHILD_FDS[i]));
            }
        }
        if (MPISPAWN_HAS_PARENT) {
            mtpmgr_send (&opcode, sizeof (int), MPISPAWN_PARENT_FD);
        }

        /* Complete operation */
        switch (opcode) {
            case PMGR_OPEN:
			    mtpmgr_debug("Completed PMGR_OPEN");
                break;
            case PMGR_CLOSE:
			    mtpmgr_debug("Completed PMGR_CLOSE");
			    quit = 1;
                break;
            case PMGR_ABORT:
			    mtpmgr_debug("Completed PMGR_ABORT");
			    mpispawn_abort (MPISPAWN_PMGR_ABORT);
                break;
            case PMGR_BARRIER:
                if (MPISPAWN_NCHILD) {
                    int j;
                    for (j = 0; j < MPISPAWN_NCHILD; j++) {
                        opcode = mtpmgr_recv_int (MPISPAWN_CHILD_FDS[j]);
                    }
                }
                if (MPISPAWN_HAS_PARENT) {
                    /* Send all the way up to root of mpispawn tree */
                    mtpmgr_send (&opcode, sizeof (opcode), MPISPAWN_PARENT_FD);

                    /* Wait for reply */
                    opcode = mtpmgr_recv_int (MPISPAWN_PARENT_FD);
                }
                
                /* Broadcast to mpispawn subtree */
                if (MPISPAWN_NCHILD) 
                    mtpmgr_bcast_subtree (&opcode, sizeof (opcode));

                /* Broadcast to my child processes */
                mtpmgr_bcast_children (&opcode, sizeof (opcode));
                break;
            case PMGR_BCAST:
                if (MPISPAWN_HAS_PARENT) {
                    mtpmgr_recv (buf, size, MPISPAWN_PARENT_FD);
                }
                if (MPISPAWN_NCHILD) 
                    mtpmgr_bcast_subtree (buf, size);
                mtpmgr_bcast_children (buf, size);
                break;
            case PMGR_GATHER:
                {
                    int my_ndata = NCHILD;
                    if (MPISPAWN_NCHILD) {
                        int j, ndata = 0;
                        for (j = 0; j < MPISPAWN_NCHILD; j++) {
                            ndata=mtpmgr_recv_int(MPISPAWN_CHILD_FDS[j]);
#define START ((size + sizeof (int)) * my_ndata)
                            mtpmgr_recv (buf + START, 
                                    (size + sizeof (int)) * ndata,
                                    MPISPAWN_CHILD_FDS[j]);
#undef START
                            my_ndata += ndata;
                        }
                    }
                    if (MPISPAWN_HAS_PARENT) {
                        mtpmgr_send (&my_ndata, sizeof (int), 
                                MPISPAWN_PARENT_FD);
                        mtpmgr_send (buf, (size + sizeof (int)) * my_ndata,
                                MPISPAWN_PARENT_FD);
                    }
                    else {
                        int rank;
                        send_buf = malloc (size * N);
                        for (i = 0; i < N; i ++) 
                        {
#define START ((size + sizeof (int)) * i)
                            rank = * (int *)(buf + START);
                            memcpy (send_buf + size * rank, 
                                    buf + (START + sizeof (int)), size);
#undef START
                        }
                        MT_ASSERT (ROOT_FD != -1);
                        mtpmgr_send (send_buf, size * N, ROOT_FD);
                    }
                }
                break;
            case PMGR_SCATTER:
                {
                    if (MPISPAWN_HAS_PARENT) {
                        mtpmgr_recv (buf, size * N, MPISPAWN_PARENT_FD);
                    }
                    if (MPISPAWN_NCHILD) {
                        mtpmgr_bcast_subtree (buf, size * N);
                    }
                    for (i = 0; i < NCHILD; i++) {
                        mtpmgr_send (buf + children[i].rank * size, size, 
                                children[i].fd);
                    }
                }
                break;
            case PMGR_ALLGATHER:
                {
                    int my_ndata = NCHILD;
                    send_buf = malloc (size * N);
                    if (MPISPAWN_NCHILD) {
                        int j, ndata = 0;
                        for (j = 0; j < MPISPAWN_NCHILD; j++) {
                            ndata =mtpmgr_recv_int(MPISPAWN_CHILD_FDS[j]);
#define START ((size + sizeof (int)) * my_ndata)
                            mtpmgr_recv (buf + START, 
                                    (size + sizeof (int)) * ndata,
                                    MPISPAWN_CHILD_FDS[j]);
#undef START
                            my_ndata += ndata;
                        }
                    }
                    if (MPISPAWN_HAS_PARENT) {
                        mtpmgr_send (&my_ndata, sizeof (int), 
                                MPISPAWN_PARENT_FD);
                        mtpmgr_send (buf, (size + sizeof (int)) * my_ndata,
                                MPISPAWN_PARENT_FD);
                        mtpmgr_recv (send_buf, size * N, MPISPAWN_PARENT_FD);
                    }
                    else {
                        int rank;
                        for (i = 0; i < N; i ++) 
                        {
#define START ((size + sizeof (int)) * i)
                            rank = * (int *)(buf + START);
                            memcpy (send_buf + size * rank, 
                                    buf + (START + sizeof (int)), size);
#undef START
                        }
                    }
                    if (MPISPAWN_NCHILD) 
                        mtpmgr_bcast_subtree (send_buf, size * N);
                    mtpmgr_bcast_children (send_buf, size * N);
                }
                break;
            case PMGR_ALLTOALL:
                {
                    int my_ndata = NCHILD;
                    send_buf = (void *) malloc (size * N * N);
                    if (MPISPAWN_NCHILD) {
                        int j, ndata = 0;
                        for (j = 0; j < MPISPAWN_NCHILD; j++) {
                            ndata =mtpmgr_recv_int(MPISPAWN_CHILD_FDS[j]);
#define START ((size * N + sizeof (int)) * my_ndata)
                            mtpmgr_recv (buf + START, 
                                    (size * N + sizeof (int)) * ndata,
                                    MPISPAWN_CHILD_FDS[j]);
#undef START
                            my_ndata += ndata;
                        }
                    }
                    if (MPISPAWN_HAS_PARENT) {
                        mtpmgr_send (&my_ndata, sizeof (int), 
                                MPISPAWN_PARENT_FD);
                        mtpmgr_send (buf, (size * N + sizeof (int)) * my_ndata,
                                MPISPAWN_PARENT_FD);
                        mtpmgr_recv (send_buf, size * N * N, 
                                MPISPAWN_PARENT_FD);
                    }
                    else {
                        int rank;
                        for (i = 0; i < N; i ++) 
                        {
#define START ((size * N + sizeof (int)) * i)
                            rank = *(int *) (buf + START);
                            memcpy (send_buf + size * N * rank,
                                    buf + (START + sizeof (int)), size * N);
                        }
                    }
                    if (MPISPAWN_NCHILD)
                        mtpmgr_bcast_subtree (send_buf, size * N * N);
                    mtpmgr_alltoallbcast (send_buf, size);
#undef START
                }
                break;
            default:
                mtpmgr_error ("Unrecognised PMGR opcode: %d\n", opcode);
        }
        if (buf) 
            free (buf);
        if (send_buf)
            free (send_buf);
    }
    mtpmgr_debug("Completed processing PMGR opcodes");
    return PMGR_SUCCESS;
}

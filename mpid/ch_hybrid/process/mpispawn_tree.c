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

#include "mpispawn_tree.h"
#include "mpirun_util.h"
#include <signal.h>
#include <string.h>
#include <errno.h>

#define NDEBUG

#ifndef NDEBUG
#include <stdio.h>
#define debug(...) fprintf(stderr, __VA_ARGS__)
#else
#define debug(...) ((void)0)
#endif

typedef struct {
    size_t num_parents, num_children;
} family_size;

static size_t id;
static size_t node_count;
static int l_socket;
static struct sockaddr_storage * node_addr;

typedef enum {
    CONN_SUCCESS,
    CONN_LIB_FAILURE,
    CONN_VERIFY_FAILURE
} CONN_STATUS;

static CONN_STATUS conn2parent(size_t parent, int * p_socket) {
    size_t p_id;

    debug("entering conn2parent [id: %d]\n", id);
    while((*p_socket = accept(l_socket, (struct sockaddr *)NULL, NULL)) < 0) {
	switch(errno) {
	    case EINTR:
	    case EAGAIN:
		continue;
	    default:
		return CONN_LIB_FAILURE;
	}
    }

    debug("verifying conn2parent [id: %d]\n", id);
    /*
     * Replace the following with a simple check of the sockaddr filled in
     * by the accept call with the sockaddr stored at node_addr[arg->id]
     */
    if(read_socket(*p_socket, &p_id, sizeof(p_id))
	    || p_id != parent
	    || write_socket(*p_socket, &id, sizeof(id))) {

	close(*p_socket);
	return CONN_VERIFY_FAILURE;
    }

    debug("leaving conn2parent [id: %d]\n", id);
    return CONN_SUCCESS;
}

static CONN_STATUS conn2children(size_t const n, size_t const children[], int
	c_socket[]) {
    size_t i, c_id;

    for(i = 0; i < n; ++i) {
	c_socket[i] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if(c_socket[i] < 0) {
	    while(i) close(c_socket[--i]);
	    return CONN_LIB_FAILURE;
	}

	if(connect(c_socket[i], (struct sockaddr *)&node_addr[children[i]],
		    sizeof(struct sockaddr)) < 0) {
	    while(i) close(c_socket[--i]);
	    return CONN_LIB_FAILURE;
	}

	if(write_socket(c_socket[i], &id, sizeof(id))
		|| read_socket(c_socket[i], &c_id, sizeof(c_id))
		|| c_id != children[i]) {
	    while(i) close(c_socket[--i]);
	    return CONN_VERIFY_FAILURE;
	}
    }

    return CONN_SUCCESS;
}

static family_size find_family(size_t const root, size_t const degree, size_t *
	parent, size_t children[]) {
    size_t offset = node_count - root;
    size_t position = (id + offset) % node_count;
    size_t c_start = degree * position + 1;
    size_t i;
    family_size fs = {0, 0};

    /*
     * Can't build a tree when nodes have a degree of 0
     */
    if(!degree) {
	return fs;
    }

    else if(position) {
	*parent = ((position - 1) / degree + root) % node_count;
	fs.num_parents = 1;
    }

    /*
     * Find the number of children I have
     */
    if(c_start < node_count) {
	if(degree > node_count - c_start) {
	    i = fs.num_children = node_count - c_start;
	}

	else {
	    i = fs.num_children = degree;
	}

	while(i--) {
	    children[i] = (c_start + i + root) % node_count;
	}
    }

    return fs;
}

extern int mpispawn_tree_init(size_t me, int req_socket) {
    size_t const degree = 10;
    size_t parent, child[degree];
    size_t i, array_size;
    int p_socket;
    family_size fs;
    int mt_degree;

    debug("entering mpispawn_tree_init [id: %d]\n", me);

    id		= me;
    l_socket    = req_socket;

    /*
     * Connect to parent
     */
    debug("[id: %d] connecting to parent\n", id);

    while((p_socket = accept(l_socket, (struct sockaddr *)NULL, NULL)) < 0) {
	    switch(errno) {
	        case EINTR:
	        case EAGAIN:
	    	continue;
	        default:
	    	perror("mpispawn_tree_init");
	    	return -1;
	    }
    }

    debug("[id: %d] connected to parent\n", id);

    if(read_socket(p_socket, &node_count, sizeof(node_count))) {
	    perror("mpispawn_tree_init");
	    return -1;
    }

    node_addr = (struct sockaddr_storage *)calloc(sizeof(struct
		    sockaddr_storage), node_count);
    if(!node_addr) {
	    perror("mpispawn_tree_init");
	    return -1;
    }

    if(read_socket(p_socket, node_addr, sizeof(struct sockaddr_storage) *
		    node_count)) {
	    perror("mpispawn_tree_init");
	    return -1;
    }

    if (read_socket(p_socket, &mt_degree, sizeof (int))) {
        perror ("mpispawn_tree_init");
        return -1;
    }

    fs = find_family(0, degree, &parent, child);

    for(i = 0; i < fs.num_children; ++i) {
	    int c_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	    if(connect(c_socket, (struct sockaddr *)&node_addr[child[i]],
            sizeof(struct sockaddr)) < 0) {
	        perror("mpispawn_tree_init");
	        return -1;
	    }

	    if(write_socket(c_socket, &node_count, sizeof(node_count))
	    	    || write_socket(c_socket, node_addr, sizeof(struct
	    		sockaddr_storage) * node_count) || write_socket(c_socket, 
                &mt_degree, sizeof(int))) {
	        return -1;
	    }

	    close(c_socket);
    }

    close(p_socket);
    debug("leaving mpispawn_tree_init [id: %d]\n", me);
    return mt_degree;
}

extern int MPISPAWN_NCHILD;
extern int MPISPAWN_HAS_PARENT;

extern int * mpispawn_tree_connect(size_t root, size_t degree) {
    size_t parent, child[degree];
    family_size fs = find_family(root, degree, &parent, child);
    int * socket_array;

    socket_array = (int *)calloc(fs.num_parents + fs.num_children, sizeof(int));
    memset (socket_array, 0xff, 
            (fs.num_parents + fs.num_children) * sizeof (int));
    MPISPAWN_NCHILD = fs.num_children;
    MPISPAWN_HAS_PARENT = fs.num_parents;

    if(!socket_array) {
	perror("calloc");
	return NULL;
    }

    /*
     * Connect to parent
     */
    if(fs.num_parents) {
	debug("[id: %d] connecting to parent\n", id);

	switch(conn2parent(parent, socket_array)) {
	    case CONN_SUCCESS:
		break;
	    case CONN_LIB_FAILURE:
		perror("mpispawn_tree_connect");
	    case CONN_VERIFY_FAILURE:
	    default:
		free(socket_array);
		return NULL;
	}

	debug("[id: %d] connected to parent\n", id);
    }

    /*
     * Connect to children
     */
    if(fs.num_children) {
	debug("[id: %d] connecting to children\n", id);

	switch(conn2children(fs.num_children, child,
		    &socket_array[fs.num_parents])) {
	    case CONN_SUCCESS:
		break;
	    case CONN_LIB_FAILURE:
		perror("mpispawn_tree_connect");
	    case CONN_VERIFY_FAILURE:
	    default:
		free(socket_array);
		return NULL;
	}
	debug("[id: %d] connected to children\n", id);
    }

    return socket_array;
}


/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
 */

/* Copyright (c) 2003-2008, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */


#ifdef _SMP_

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/mman.h>
#include <errno.h>
#include <pthread.h>
#include <psm64.h>

#ifdef _AFFINITY_
#include <sched.h>
#endif /*_AFFINITY_*/

#ifdef MAC_OSX
#include <netinet/in.h>
#endif

#include "psmpriv.h"
#include "mpid_smpi.h"
#include "coll_shmem.h"
#include <stdio.h>

shmem_coll_region *shmem_coll;
struct shmem_coll_mgmt shmem_coll_obj;
extern struct smpi_var smpi;

int shmem_coll_size = 0;
char *shmem_file = NULL;

char hostname[SHMEM_COLL_HOSTNAME_LEN];
int my_rank;
int shmem_coll_max_msg_size = (1<<16);
extern int shmem_coll_blocks;
extern int shmem_coll_max_msg_size;
extern int shmem_bcast_leaders;

int shmem_coll_num_comm;
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_SHMEM_COLL_init(void)
{
    int pagesize = getpagesize();
#ifdef _X86_64_
    volatile char tmpchar;
#endif

    shmem_coll_num_comm = shmem_coll_blocks; 

    /* add pid for unique file name */
    shmem_file = (char *) malloc(sizeof(char) * (SHMEM_COLL_HOSTNAME_LEN + 26 + PID_CHAR_LEN));


    /* unique shared file name */
    sprintf(shmem_file, "/dev/shm/ib_shmem_coll-%d-%s-%d.tmp",
            psmdev.global_id, psmdev.my_name, getuid());


    /* open the shared memory file */
    shmem_coll_obj.fd = open(shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    if (shmem_coll_obj.fd < 0) {
        /* Fallback */
        sprintf(shmem_file, "/tmp/ib_shmem_coll-%d-%s-%d.tmp",
            psmdev.global_id, psmdev.my_name, getuid());
        shmem_coll_obj.fd = open(shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        if (shmem_coll_obj.fd < 0) {
            perror("open");
            fprintf(stderr, "[%d] shmem_coll_init:error in opening " "shared memory file <%s>: %d\n",
                         my_rank, shmem_file, errno);
            return -1;
	}
    }


    shmem_coll_size = SMPI_ALIGN (SHMEM_COLL_BUF_SIZE + pagesize) + SMPI_CACHE_LINE_SIZE;

    if (smpi.my_local_id == 0) {
        if (ftruncate(shmem_coll_obj.fd, 0)) {
            /* to clean up tmp shared file */
            unlink(shmem_file);
            fprintf(stderr,  "[%d] shmem_coll_init:error in ftruncate to zero "
                             "shared memory file: %d\n", my_rank, errno);
            return -1;
        }

        /* set file size, without touching pages */
        if (ftruncate(shmem_coll_obj.fd, shmem_coll_size)) {
            /* to clean up tmp shared file */
            unlink(shmem_file);
            fprintf(stderr,  "[%d] shmem_coll_init:error in ftruncate to size "
                             "shared memory file: %d\n", my_rank, errno);
            return -1;
        }

/* Ignoring optimal memory allocation for now */
#ifndef _X86_64_
        {
            char *buf;
            buf = (char *) calloc(shmem_coll_size + 1, sizeof(char));
            if (write(shmem_coll_obj.fd, buf, shmem_coll_size) != shmem_coll_size) {
                printf("[%d] shmem_coll_init:error in writing " "shared memory file: %d\n", my_rank, errno);
                free(buf);
                return -1;
            }
            free(buf);
        }

#endif
        if (lseek(shmem_coll_obj.fd, 0, SEEK_SET) != 0) {
            /* to clean up tmp shared file */
            unlink(shmem_file);
            fprintf(stderr,  "[%d] shmem_coll_init:error in lseek "
                             "on shared memory file: %d\n",
                             my_rank, errno);
            return -1;
        }

    }

    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_Mmap
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_SHMEM_COLL_Mmap()
{
    int i = 0, j = 0;
    shmem_coll_obj.mmap_ptr = mmap(0, shmem_coll_size,
                         (PROT_READ | PROT_WRITE), (MAP_SHARED), shmem_coll_obj.fd,
                         0);
    if (shmem_coll_obj.mmap_ptr == (void *) -1) {
        /* to clean up tmp shared file */
        unlink(shmem_file);
        fprintf(stderr,  "[%d] shmem_coll_mmap:error in mmapping "
                         "shared memory: %d\n", my_rank, errno);
        return -1;
    }
    shmem_coll = (shmem_coll_region *) shmem_coll_obj.mmap_ptr;
    
    
    shmem_coll_obj.child_complete_bcast = malloc(sizeof(int*)*shmem_coll_num_comm);
    shmem_coll_obj.root_complete_bcast = malloc(sizeof(int*)*shmem_coll_num_comm);
    shmem_coll_obj.child_complete_gather = malloc(sizeof(int*)*shmem_coll_num_comm);
    shmem_coll_obj.root_complete_gather = malloc(sizeof(int*)*shmem_coll_num_comm);
    shmem_coll_obj.barrier_gather = malloc(sizeof(int*)*shmem_coll_num_comm);
    shmem_coll_obj.barrier_bcast = malloc(sizeof(int*)*shmem_coll_num_comm);
    
    shmem_coll_obj.child_complete_bcast[0] = (int*)((char*)(shmem_coll_obj.mmap_ptr) + sizeof(shmem_coll));
    shmem_coll_obj.root_complete_bcast[0] = 
        (int*)(shmem_coll_obj.child_complete_bcast[0] + shmem_coll_num_comm*smpi.num_local_nodes);
    shmem_coll_obj.child_complete_gather[0] =
        (int*)(shmem_coll_obj.child_complete_bcast[0] + 2*shmem_coll_num_comm*smpi.num_local_nodes);
    shmem_coll_obj.root_complete_gather[0] =
        (int*)(shmem_coll_obj.child_complete_bcast[0] + 3*shmem_coll_num_comm*smpi.num_local_nodes);
    shmem_coll_obj.barrier_gather[0] = 
        (int*)(shmem_coll_obj.child_complete_bcast[0] + 4*shmem_coll_num_comm*smpi.num_local_nodes);
    shmem_coll_obj.barrier_bcast[0] =
        (int*)(shmem_coll_obj.child_complete_bcast[0] + 5*shmem_coll_num_comm*smpi.num_local_nodes);
      

    for (j=1;j<shmem_coll_num_comm;j++){
         shmem_coll_obj.child_complete_bcast[j] =  
             (int*)(shmem_coll_obj.child_complete_bcast[0] + j*smpi.num_local_nodes);
        shmem_coll_obj.child_complete_gather[j] =
             (int*)(shmem_coll_obj.child_complete_gather[0] + j*smpi.num_local_nodes);
        shmem_coll_obj.root_complete_bcast[j] =
             (int*)(shmem_coll_obj.root_complete_bcast[0] + j*smpi.num_local_nodes);
        shmem_coll_obj.root_complete_gather[j] =
             (int*)(shmem_coll_obj.root_complete_gather[0] + j*smpi.num_local_nodes);
        shmem_coll_obj.barrier_gather[j] =
             (int*)(shmem_coll_obj.barrier_gather[0] + j*smpi.num_local_nodes);
        shmem_coll_obj.barrier_bcast[j] =
             (int*)(shmem_coll_obj.barrier_bcast[0] + j*smpi.num_local_nodes);
    }

    shmem_coll_obj.shmem_avail = (char*)(shmem_coll_obj.child_complete_bcast[0] + 
				 6*shmem_coll_num_comm*smpi.num_local_nodes);

    if (smpi.my_local_id == 0){
        memset(shmem_coll_obj.mmap_ptr, 0, shmem_coll_size);
        for(j=0; j < shmem_coll_num_comm; j++){
            for (i = 0; i < smpi.num_local_nodes; i++){
                shmem_coll_obj.child_complete_bcast[j][i] = 1;
            }
            for (i = 0; i < smpi.num_local_nodes; i++){
                 shmem_coll_obj.root_complete_gather[j][i] = 1;
            }
        }
        for (j=0;j<shmem_coll_blocks;j++){
            shmem_coll_obj.shmem_avail[j] = 1;
        }
        pthread_spin_init(&shmem_coll->shmem_coll_lock, PTHREAD_PROCESS_SHARED);
    }
    

     	
    shmem_coll_obj.shmem_coll_buf = (char*)(shmem_coll_obj.child_complete_bcast[0] + 
				    6*shmem_coll_num_comm*smpi.num_local_nodes + shmem_coll_blocks);

    return MPI_SUCCESS;
}



#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_SHMEM_COLL_finalize()
{
    /* unmap the shared memory file */
    munmap(shmem_coll_obj.mmap_ptr, shmem_coll_size);

    close(shmem_coll_obj.fd);
    
    free(shmem_file);
    return MPI_SUCCESS;
}

void MPID_SHMEM_COLL_Unlink(){
        unlink(shmem_file);
}

/* Shared memory gather: rank zero is the root always*/
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_Gather
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPID_SHMEM_COLL_GetShmemBuf(int size, int rank, int shmem_comm_rank, void** output_buf)
{
    int i,myid;
    char* shmem_coll_buf = shmem_coll_obj.shmem_coll_buf;

    myid = rank;

    if (myid == 0){
        
        for (i=1; i < size; i++){ 
            while (shmem_coll_obj.child_complete_gather[shmem_comm_rank][i] == 0)
            {
                MPID_DeviceCheck(MPID_NOTBLOCKING);
            };
        }
        /* Set the completion flags back to zero */
        for (i=1; i < size; i++){ 
            shmem_coll_obj.child_complete_gather[shmem_comm_rank][i] = 0;
        }
        *output_buf = (char*)shmem_coll_buf + shmem_comm_rank*SHMEM_COLL_BLOCK_SIZE;
    }
    else{
        while (shmem_coll_obj.root_complete_gather[shmem_comm_rank][myid] == 0)
        {
            MPID_DeviceCheck(MPID_NOTBLOCKING);
        };
        shmem_coll_obj.root_complete_gather[shmem_comm_rank][myid] = 0;
        *output_buf = (char*)shmem_coll_buf + shmem_comm_rank*SHMEM_COLL_BLOCK_SIZE;
    }
}



void MPID_SHMEM_COLL_GetShmemBcastBuf(void** output_buf, void* buffer){
    char* shmem_coll_buf = (char*)(buffer);

    *output_buf = (char*)shmem_coll_buf + 3*shmem_bcast_flags + shmem_bcast_leaders*SHMEM_BCAST_METADATA;
}

void signal_local_processes(int step, int index, char* send_buf, int offset, int bytes, void* mmap_ptr){

    void* buffer;
    volatile char* bcast_flags;
    int metadata_offset = 3*shmem_bcast_flags;
    char* shmem_coll_buf = (char*)(mmap_ptr);
    bcast_flags = (char*)shmem_coll_buf + index*shmem_bcast_flags;
    char* tmp = (char*)shmem_coll_buf + metadata_offset + step*SHMEM_BCAST_METADATA;

    buffer = (aint_t*)tmp;
    *((aint_t*)buffer) = (aint_t)send_buf;	
    buffer = (int*)(tmp + sizeof(aint_t));
    *((int*)buffer) = offset;
    buffer = (int*)(tmp + sizeof(aint_t) + sizeof(int));
    *((int*)buffer) = bytes;

    bcast_flags[step] = 1;
    
    /* Clear the bcast flags for the next Bcast */
    tmp = (char*)shmem_coll_buf + ((index + 1)%3)*shmem_bcast_flags;
    memset((void*) tmp,0, shmem_bcast_flags);
}

void wait_for_signal(int step, int index, char** output_buf, int* offset, int* bytes, void* mmap_ptr){

    char* shmem_coll_buf = (char*)(mmap_ptr);
    volatile char* bcast_flags;
    bcast_flags = (char*)shmem_coll_buf +  index*shmem_bcast_flags;
    int metadata_offset = 3*shmem_bcast_flags;
    char* tmp = (char*)shmem_coll_buf + 3*shmem_bcast_flags + step*SHMEM_BCAST_METADATA;
    void* buffer;
    while (bcast_flags[step] == 0){
	    MPID_DeviceCheck(MPID_NOTBLOCKING);
    }	
    buffer = (aint_t*)tmp;
    buffer = (int*)(tmp + sizeof(aint_t));
    *offset = *((int*)buffer);
    *output_buf = (char*)(mmap_ptr) + 3*shmem_bcast_flags + shmem_bcast_leaders*SHMEM_BCAST_METADATA + *offset;
    buffer = (int*)(tmp + sizeof(aint_t) + sizeof(int));
    *bytes = *((int*)buffer);

}

void MPID_SHMEM_COLL_SetGatherComplete(int size, int rank, int shmem_comm_rank)
{

    int i, myid;
    myid = rank;

    if (myid == 0){
        for (i=1; i < size; i++){ 
            shmem_coll_obj.root_complete_gather[shmem_comm_rank][i] = 1;
        }
    }
    else{
        shmem_coll_obj.child_complete_gather[shmem_comm_rank][myid] = 1;
    }
}

void MPID_SHMEM_COLL_Barrier_gather(int size, int rank, int shmem_comm_rank)
{
    int i, myid;
    myid = rank;

    if (rank == 0){
        for (i=1; i < size; i++){ 
            while (shmem_coll_obj.barrier_gather[shmem_comm_rank][i] == 0)
            {
                MPID_DeviceCheck(MPID_NOTBLOCKING);
            }
        }
        for (i=1; i < size; i++){ 
            shmem_coll_obj.barrier_gather[shmem_comm_rank][i] = 0; 
        }
    }
    else{
        shmem_coll_obj.barrier_gather[shmem_comm_rank][myid] = 1;
    }
}

void MPID_SHMEM_COLL_Barrier_bcast(int size, int rank, int shmem_comm_rank)
{
    int i, myid;
    myid = rank;

    if (rank == 0){
        for (i=1; i < size; i++){ 
            shmem_coll_obj.barrier_bcast[shmem_comm_rank][i] = 1;
        }
    }
    else{
        while (shmem_coll_obj.barrier_bcast[shmem_comm_rank][myid] == 0)
        {
            MPID_DeviceCheck(MPID_NOTBLOCKING);
        }
        shmem_coll_obj.barrier_bcast[shmem_comm_rank][myid] = 0;
    }
                MPID_DeviceCheck(MPID_NOTBLOCKING);
}

int MPID_Is_local(int i){
    if (smpi.local_nodes[i] != -1){
        return 1;
    } 
    else{
        return 0;
    }
}

int MPID_SHMEM_BCAST_init(int file_size, int shmem_comm_rank, int my_local_rank, int* bcast_seg_size, char** bcast_shmem_file, int* fd)
{
    int pagesize = getpagesize();
#ifdef _X86_64_
    volatile char tmpchar;
#endif


    /* add pid for unique file name */
    *bcast_shmem_file = (char *) malloc(sizeof(char) * (SHMEM_COLL_HOSTNAME_LEN + 26 + PID_CHAR_LEN));


    /* unique shared file name */
    sprintf(*bcast_shmem_file, "/dev/shm/ib_shmem_bcast_coll-%d-%s-%d-%d.tmp",
            psmdev.global_id, psmdev.my_name, getuid(),shmem_comm_rank);


    /* open the shared memory file */
    *fd = open(*bcast_shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    if (*fd < 0) {
        /* Fallback */
        sprintf(*bcast_shmem_file, "/tmp/ib_shmem_bcast_coll-%d-%s-%d-%d.tmp",
            psmdev.global_id, psmdev.my_name, getuid(),shmem_comm_rank);
        *fd = open(*bcast_shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        if (*fd < 0) {
            perror("open");
            fprintf(stderr, "[%d] shmem_coll_init:error in opening " "shared memory file <%s>: %d\n",
                         psmdev.global_id, *bcast_shmem_file, errno);
            return 0;
	}
    }


    *bcast_seg_size = SMPI_ALIGN (file_size + pagesize) + SMPI_CACHE_LINE_SIZE;

    if (my_local_rank == 0) {
        if (ftruncate(*fd, 0)) {
            /* to clean up tmp shared file */
            unlink(*bcast_shmem_file);
            fprintf(stderr,  "[%d] shmem_coll_init:error in ftruncate to zero "
                             "shared memory file: %d\n", my_rank, errno);
            return 0;
        }

        /* set file size, without touching pages */
        if (ftruncate(*fd, *bcast_seg_size)) {
            /* to clean up tmp shared file */
            unlink(*bcast_shmem_file);
            fprintf(stderr,  "[%d] shmem_coll_init:error in ftruncate to size "
                             "shared memory file: %d\n", my_rank, errno);
            return 0;
        }

/* Ignoring optimal memory allocation for now */
#ifndef _X86_64_
        {
            char *buf;
            buf = (char *) calloc(*bcast_seg_size + 1, sizeof(char));
            if (write(*fd, buf, *bcast_seg_size) != *bcast_seg_size) {
                printf("[%d] shmem_coll_init:error in writing " "shared memory file: %d\n", my_rank, errno);
                free(buf);
                return 0;
            }
            free(buf);
        }

#endif
        if (lseek(*fd, 0, SEEK_SET) != 0) {
            /* to clean up tmp shared file */
            unlink(*bcast_shmem_file);
            fprintf(stderr,  "[%d] shmem_coll_init:error in lseek "
                             "on shared memory file: %d\n",
                             my_rank, errno);
            return 0;
        }

    }

    return 1;
}

int MPID_SHMEM_BCAST_mmap(void** mmap_ptr, int bcast_seg_size, int fd, int my_local_rank, char* bcast_shmem_file)
{
    int i = 0, j = 0;
    *mmap_ptr = mmap(0, bcast_seg_size,
                         (PROT_READ | PROT_WRITE), (MAP_SHARED), fd, 0);
    if (*mmap_ptr == (void *) -1) {
        /* to clean up tmp shared file */
        unlink(bcast_shmem_file);
        fprintf(stderr,  "[%d] shmem_coll_mmap:error in mmapping "
                         "shared memory: %d\n", my_local_rank, errno);
        return -1;
    }

    if (my_local_rank == 0){
        memset(*mmap_ptr, 0, bcast_seg_size);
    }
    
    return MPI_SUCCESS;
}
#endif



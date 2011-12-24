/*
 * Copyright (c) 2002-2010, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package, developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL) and headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copright file COPYRIGHT_MVAPICH in the top level MPICH directory.
 *
 */
#if (defined( _SMP_) && (defined(CH_GEN2) || defined(CH_SMP) || defined(CH_GEN2_UD) || defined(CH_PSM)))

#include <sys/mman.h>
#include "mpiimpl.h"
#include "mpimem.h"
#include "mpid.h"
#include "mpiops.h"
#include "mpipt2pt.h"

#ifdef CH_GEN2
#include "../../mpid/ch_gen2/coll_shmem.h"
#elif defined(CH_SMP)
#include "../../mpid/ch_smp/coll_shmem.h"
#elif defined(CH_GEN2_UD)
#include "../../mpid/ch_hybrid/coll_shmem.h"
#elif CH_PSM
#include "../../mpid/ch_psm/coll_shmem.h"
#endif

#include <pthread.h>
#define MAX_SHMEM_COMM  4
#define MAX_NUM_COMM    10000
#define MAX_ALLOWED_COMM   250

unsigned int comm_registry [MAX_NUM_COMM];
unsigned int comm_registered = 0;
unsigned int comm_count = 0;
int shmem_coll_blocks=16;
int shmem_comm_count = 0;
extern shmem_coll_region *shmem_coll;
extern int disable_shmem_bcast;
extern int viadev_use_allgather_new;
extern struct shmem_coll_mgmt shmem_coll_obj;

extern int MPID_Is_local(int);

void clear_2level_comm (struct MPIR_COMMUNICATOR* comm_ptr)
{
    comm_ptr->shmem_coll_ok = 0;
    comm_ptr->leader_map  = NULL;
    comm_ptr->leader_rank = NULL;
    comm_ptr->bcast_mmap_ptr = NULL;
    comm_ptr->bcast_shmem_file = NULL;
    comm_ptr->bcast_fd = -1;
    comm_ptr->bcast_index = 0;
    comm_ptr->allg_cyclic_ok = 0;
    comm_ptr->new_comm = MPI_COMM_NULL;
    comm_ptr->leader_comm = MPI_COMM_NULL;
    comm_ptr->shmem_comm = MPI_COMM_NULL;
}

void free_2level_comm (struct MPIR_COMMUNICATOR* comm_ptr)
{
    if (comm_ptr->leader_map)  { free(comm_ptr->leader_map);  }
    if (comm_ptr->leader_rank) { free(comm_ptr->leader_rank); }

    if (comm_ptr->bcast_mmap_ptr){
	munmap(comm_ptr->bcast_mmap_ptr, comm_ptr->bcast_seg_size);
    }	
    if (comm_ptr->bcast_fd >= 0){
	close(comm_ptr->bcast_fd);
    }	
    if (comm_ptr->bcast_shmem_file){
    	free(comm_ptr->bcast_shmem_file);
    }	
	
    if (comm_ptr->new_comm != MPI_COMM_NULL){
	    MPI_Comm_free(&(comm_ptr->new_comm));
	    MPI_Group_free(&(comm_ptr->new_group));
	    free(comm_ptr->new_ranks);
    }

    if (comm_ptr->leader_comm != MPI_COMM_NULL) { 
	    MPI_Comm_free(&(comm_ptr->leader_comm));
    }

    if (comm_ptr->shmem_comm != MPI_COMM_NULL)  { 
        struct MPIR_COMMUNICATOR* shmem_ptr;
        shmem_ptr= MPIR_GET_COMM_PTR(comm_ptr->shmem_comm);

        int my_local_id;
        MPI_Comm_rank(comm_ptr->shmem_comm, &my_local_id);

        if ((my_local_id == 0)&&(comm_ptr->shmem_coll_ok == 1)){ 
            pthread_spin_lock(&shmem_coll->shmem_coll_lock);
            shmem_coll_obj.shmem_avail[shmem_ptr->shmem_comm_rank] = 1;		    	
            pthread_spin_unlock(&shmem_coll->shmem_coll_lock);
        }

        MPI_Comm_free(&(comm_ptr->shmem_comm));
    }


    clear_2level_comm(comm_ptr);
}

void create_2level_comm (struct MPIR_COMMUNICATOR* comm_ptr, int size, int my_rank){

    struct MPIR_COMMUNICATOR* comm_world_ptr;
    comm_world_ptr = MPIR_GET_COMM_PTR(MPI_COMM_WORLD);


    int* shmem_group = malloc(sizeof(int) * size);
    if (NULL == shmem_group){
        printf("Couldn't malloc shmem_group\n");
        exit(0);
    }

    /* Creating local shmem group */
    int i, shmem_grp_size, local_rank=0;
    int grp_index = 0;

    for (i=0; i < size ; i++){
       if ((my_rank == i) || (MPID_Is_local(comm_ptr->lrank_to_grank[i]) == 1)){
           shmem_group[grp_index] = i;
           if (my_rank == i){
               local_rank = grp_index;
           }
           grp_index++;
       }  
    } 
    shmem_grp_size = grp_index;
    
    /* Creating leader group */
    int leader = 0;
    leader = shmem_group[0];

    /* Gives the mapping to any process's leader in comm */
    comm_ptr->leader_map = malloc(sizeof(int) * size);
    if (NULL == comm_ptr->leader_map){
        printf("Couldn't malloc group\n");
        exit(0);
    }

    MPI_Allgather (&leader, 1, MPI_INT , comm_ptr->leader_map, 1, MPI_INT, comm_ptr->self);

    int leader_group_size=0;
    int* leader_group = malloc(sizeof(int) * size);
    if (NULL == leader_group){
        printf("Couldn't malloc leader_group\n");
        exit(0);
    }

    /* Gives the mapping from leader's rank in comm to 
     * leader's rank in leader_comm */
    comm_ptr->leader_rank = malloc(sizeof(int) * size);
    if (NULL == comm_ptr->leader_rank){
        printf("Couldn't malloc marker\n");
        exit(0);
    }

    for (i=0; i < size ; i++){
         comm_ptr->leader_rank[i] = -1;
    }
    int* group = comm_ptr->leader_map;
    grp_index = 0;
    for (i=0; i < size ; i++){
        if (comm_ptr->leader_rank[(group[i])] == -1){
            comm_ptr->leader_rank[(group[i])] = grp_index;
            leader_group[grp_index++] = group[i];
        }
    }
    leader_group_size = grp_index;
    comm_ptr->leader_group_size = leader_group_size;

    MPI_Group subgroup1, comm_group;
    MPI_Comm_group(comm_ptr->self, &comm_group);
    MPI_Group_incl(comm_group, leader_group_size, leader_group, &subgroup1);
    MPI_Comm_create(comm_ptr->self, subgroup1, &(comm_ptr->leader_comm));
    int leader_rank;
    if (comm_ptr->leader_comm != MPI_COMM_NULL){
        MPI_Comm_rank(comm_ptr->leader_comm, &leader_rank);
    }
    free(leader_group);

    struct MPIR_COMMUNICATOR* leader_ptr;
    leader_ptr = MPIR_GET_COMM_PTR( comm_ptr->leader_comm );
    
    MPI_Comm_split(comm_ptr->self, leader, local_rank, &(comm_ptr->shmem_comm));
    struct MPIR_COMMUNICATOR* shmem_ptr;
    shmem_ptr= MPIR_GET_COMM_PTR(comm_ptr->shmem_comm);
    shmem_ptr->comm_coll->shmem_coll_ok = 0;	
    //shmem_ptr->comm_coll = shmem_ptr;
    int my_local_id, input_flag =0, output_flag=0;
    MPI_Comm_rank(comm_ptr->shmem_comm, &my_local_id);

    if (my_local_id == 0){
        pthread_spin_lock(&shmem_coll->shmem_coll_lock);
        shmem_comm_count = shmem_coll_blocks;

        for (i=0; i < shmem_coll_blocks;i++){
            if (shmem_coll_obj.shmem_avail[i]== 1){
                shmem_comm_count = i;
                shmem_coll_obj.shmem_avail[i] = 0;
                break;
            }
        }
	
        pthread_spin_unlock(&shmem_coll->shmem_coll_lock);
    }

     
    MPI_Bcast (&shmem_comm_count, 1, MPI_INT, 0, comm_ptr->shmem_comm);

    if (shmem_comm_count < shmem_coll_blocks){
        shmem_ptr->shmem_comm_rank = shmem_comm_count;
        input_flag = 1;
    }
    else{
        input_flag = 0;
    }

    MPI_Allreduce(&input_flag, &output_flag, 1, MPI_INT, MPI_LAND, comm_ptr->self);

    comm_ptr->bcast_shmem_file = NULL;
#if 1
    if (viadev_use_allgather_new){
	    int is_contig =1, check_leader =1, check_size=1, is_local_ok=0,is_block=0;
	    int PPN;

	    MPI_Bcast (&leader_rank, 1, MPI_INT, 0, comm_ptr->shmem_comm);

	    for ( i=1; i < shmem_grp_size; i++ ){
		    if (shmem_group[i] != shmem_group[i-1]+1){
			    is_contig =0;
			    break;
		    }
	    }

	    if (leader != (shmem_grp_size*leader_rank)){
		    check_leader=0;
	    }

	    if (shmem_grp_size != (size/leader_group_size)){
		    check_size=0;
	    }

	    is_local_ok = is_contig && check_leader && check_size;

	    MPI_Allreduce(&is_local_ok, &is_block, 1, MPI_INT, MPI_LAND, comm_ptr->self);

	    if (is_block){
		    int counter=0,j;
		    comm_ptr->new_ranks = (int*) malloc(sizeof(int)*size);

		    PPN = shmem_grp_size;
		    for (j=0; j < PPN; j++){
			    for (i=0; i < leader_group_size; i++){
				    comm_ptr->new_ranks[counter] = j + i*PPN;
				    counter++;
			    }
		    }

		    MPI_Group_incl(comm_group, size, comm_ptr->new_ranks, &(comm_ptr->new_group));
		    MPI_Comm_create(comm_ptr->self, comm_ptr->new_group, &(comm_ptr->new_comm));
		    comm_ptr->allg_cyclic_ok=1;

	    }
    }
#endif

    if (output_flag == 1){
        comm_ptr->shmem_coll_ok = 1;
    }
    else{
        comm_ptr->shmem_coll_ok = 0;
    }
    MPI_Group_free(&subgroup1);
    MPI_Group_free(&comm_group);

    ++comm_count;
    free(shmem_group);
}

int check_comm_registry(struct MPIR_COMMUNICATOR* comm)
{
    int context_id = 0, i =0;
    context_id = comm->recv_context;

    for (i = 0; i < comm_registered; i++){
        if (comm_registry[i] == context_id){
            return 1;
        }
    }

    return 0;
}

#endif

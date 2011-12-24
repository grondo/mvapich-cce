/*
 *  $Id: comm_util.c,v 1.3 2006/12/05 17:19:15 mamidala Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2002-2010, The Ohio State University. All rights
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

#include "mpiimpl.h"
#include "mpimem.h"
/* For MPIR_COLLOPS */
#include "mpicoll.h"

#ifdef _SMP_
extern int disable_shmem_allreduce;
extern int enable_shmem_collectives;
extern int split_comm ;
extern int MPIR_Has_been_initialized;
#endif

#ifndef CH_GEN2
extern int viadev_use_on_demand;
#endif

#if (defined(VIADEV_RPUT_SUPPORT) || defined(VIADEV_RGET_SUPPORT))
extern void comm_exch_addr(struct MPIR_COMMUNICATOR*);
extern void set_local_offsets(struct MPIR_COMMUNICATOR* comm);
extern void coll_prepare_flags(struct Collbuf*);
extern void clear_2level_comm (struct MPIR_COMMUNICATOR*);
extern void create_2level_comm (struct MPIR_COMMUNICATOR*, int, int);
#endif

/*

MPIR_Comm_make_coll - make a hidden collective communicator
                      from an inter- or intra-communicator assuming
		      that an appropriate number of contexts
		      have been allocated.  An inter-communicator
		      collective can only be made from another
		      inter-communicator.

See comm_create.c for code that creates a visible communicator.
*/
int MPIR_Comm_make_coll ( struct MPIR_COMMUNICATOR *comm, 
			  MPIR_COMM_TYPE comm_type )
{
  struct MPIR_COMMUNICATOR *new_comm;
  int      mpi_errno;

#ifdef MCST_SUPPORT
  int i;
#endif  

  MPIR_ALLOC(new_comm,NEW(struct MPIR_COMMUNICATOR),comm,
	     MPIR_ERRCLASS_TO_CODE(MPI_ERR_COMM,MPIR_ERR_COMM_NAME),
			"internal-Comm_make_coll" );
  MPIR_Comm_init( new_comm, comm, comm_type );
  MPIR_Attr_dup_tree ( comm, new_comm );

  if (comm_type == MPIR_INTRA) {
    new_comm->recv_context    = comm->recv_context + 1;
    new_comm->send_context    = new_comm->recv_context;
    MPIR_Group_dup ( comm->local_group, &(new_comm->group) );
    MPIR_Group_dup ( comm->local_group, &(new_comm->local_group) );
  }
  else {
    new_comm->recv_context    = comm->recv_context + 1;
    new_comm->send_context    = comm->send_context + 1;
    MPIR_Group_dup ( comm->group, &(new_comm->group) );
    MPIR_Group_dup ( comm->local_group, &(new_comm->local_group) );
  }
  new_comm->local_rank     = new_comm->local_group->local_rank;
  new_comm->lrank_to_grank = new_comm->group->lrank_to_grank;
  new_comm->np             = new_comm->group->np;
  new_comm->collops        = NULL;

  new_comm->comm_coll       = new_comm;  /* a circular reference to myself */
  comm->comm_coll           = new_comm;

  /* Place the same operations on both the input (comm) communicator and
     the private copy (new_comm) */
  MPIR_Comm_collops_init( new_comm, comm_type);
  MPIR_Comm_collops_init( comm, comm_type);

  /* The error handler for the collective communicator is ALWAYS
     errors return */
  MPI_Errhandler_set( new_comm->self, MPI_ERRORS_RETURN );

  /* The MPID_Comm_init routine needs the size of the local group, and
     reads it from the new_comm structure */
  if ((mpi_errno = MPID_CommInit( comm, new_comm ))) 
      return mpi_errno;
  /* Is that a storage leak ? Shouldn't we free new_comm if the init
   * fails ? (Or will it get freed higher up ??)
   */
  
  new_comm->comm_name = 0;

  /* Remember it for the debugger */
  MPIR_Comm_remember(new_comm);
  
#ifdef MCST_SUPPORT
   
  if (comm_type == MPIR_INTRA){
    
    new_comm->is_mcst_ok = 1;  
      
    if (new_comm->np != MPIR_COMM_WORLD->np){
        new_comm->is_mcst_ok = 0;
    }
    else{
        
        for (i = 0; i < new_comm->np; i++){
        if (new_comm->lrank_to_grank[i] != MPIR_COMM_WORLD->lrank_to_grank[i]){
            
           new_comm->is_mcst_ok = 0;
           break;
        }
        }

    }            
    }      
  
#endif  

  
#if (defined( _SMP_) && (defined(CH_GEN2) || defined(CH_SMP) || defined(CH_GEN2_UD) || defined(CH_PSM)))
#if 1
  if ((MPIR_Has_been_initialized == 1) && (enable_shmem_collectives)&&(comm_type == MPIR_INTRA)){
      clear_2level_comm(new_comm);
      if (split_comm == 1){
          split_comm = 0;
          create_2level_comm(new_comm, new_comm->np, new_comm->local_rank);
          split_comm = 1;
      }
  }
#endif
#endif

  MPID_THREAD_LOCK_INIT(new_comm->ADIctx,new_comm);
  return(MPI_SUCCESS);
}


/*+

MPIR_Comm_N2_prev - retrieve greatest power of two < size of Comm.

+*/
int MPIR_Comm_N2_prev ( 
	struct MPIR_COMMUNICATOR *comm,
	int *N2_prev)
{
  (*N2_prev) = comm->group->N2_prev;
  return (MPI_SUCCESS);
}


/*+
  MPIR_Dump_comm - utility function to dump a communicator 
+*/
int MPIR_Dump_comm ( 
	struct MPIR_COMMUNICATOR * comm )
{
  int  rank;

  MPIR_Comm_rank ( MPIR_COMM_WORLD, &rank );

  PRINTF("[%d] ----- Dumping communicator -----\n", rank );
  if (comm->comm_type == MPIR_INTRA) {
    PRINTF("[%d] Intra-communicator\n",rank);
    PRINTF("[%d] Group\n",rank);
    MPIR_Dump_group ( comm->group );
  }
  else {
    PRINTF("[%d]\tInter-communicator\n",rank);
    PRINTF("[%d] Local group\n",rank);
    MPIR_Dump_group ( comm->local_group );
    PRINTF("[%d] Remote group\n",rank);
    MPIR_Dump_group ( comm->group );
  }
  PRINTF ("[%d] Ref count = %d\n",rank,comm->ref_count);
  /* Assumes context stored as a unsigned long (?) */
  PRINTF ("[%d] Send = %u   Recv =%u\n",
          rank,comm->send_context,comm->recv_context);
  PRINTF ("[%d] permanent = %d\n",rank,comm->permanent);
  return (MPI_SUCCESS);
}

/*+
  MPIR_Intercomm_high - determine a high value for an
                        inter-communicator
+*/
int MPIR_Intercomm_high ( 
	struct MPIR_COMMUNICATOR * comm,
	int *high)
{
  MPI_Status status;
  struct MPIR_COMMUNICATOR *   inter = comm->comm_coll;
  struct MPIR_COMMUNICATOR *   intra = inter->comm_coll;
  int        rank, rhigh;

  MPIR_Comm_rank ( comm, &rank );

  /* Node 0 determines high value */
  if (rank == 0) {

    /* "Normalize" value for high */
    if (*high)
      (*high) = 1;
    else
      (*high) = 0;

    /* Get the remote high value from remote node 0 and determine */
    /* appropriate high */
    MPI_Sendrecv(  high, 1, MPI_INT, 0, 0, 
                 &rhigh, 1, MPI_INT, 0, 0, inter->self, &status);
    if ( (*high) == rhigh ) {
      if ( comm->group->lrank_to_grank[0] < 
           comm->local_group->lrank_to_grank[0] )
        (*high) = 1;
      else
        (*high) = 0;
    }
  }

  /* Broadcast high value to all */
  MPI_Bcast ( high, 1, MPI_INT, 0, intra->self );
  return (MPI_SUCCESS);
}


/*

MPIR_Comm_init  - Initialize some of the elements of a communicator from 
                  an existing one.

		  This can't return anything but success, so I've changed it
		  to void.  gcc in check mode complains about not using the
		  result, even when cast to void!
*/
void MPIR_Comm_init ( 
	struct MPIR_COMMUNICATOR *new_comm, 
	struct MPIR_COMMUNICATOR *comm,
	MPIR_COMM_TYPE comm_type)
{
  MPIR_SET_COOKIE(new_comm,MPIR_COMM_COOKIE);
  new_comm->self = (MPI_Comm) MPIR_FromPointer(new_comm);
  new_comm->ADIctx	       = comm->ADIctx;
  new_comm->comm_type	       = comm_type;
  new_comm->comm_cache	       = 0;
  new_comm->error_handler      = 0;
  new_comm->use_return_handler = 0;
  MPI_Errhandler_set( new_comm->self, comm->error_handler );
  new_comm->ref_count	       = 1;
  new_comm->permanent	       = 0;
  new_comm->collops	       = 0;
  new_comm->attr_cache	       = 0;
}

/*+

MPIR_Comm_remember - remember the communicator on the list of 
                     all communicators, and bump the sequence number.
		     Do this only once the communicator is well enough
		     constructed that it makes sense for the debugger	
		     to see it.

+*/
void MPIR_Comm_remember( 
	struct MPIR_COMMUNICATOR * new_comm)
{
  /* What about thread locking ? */
  new_comm->comm_next = MPIR_All_communicators.comm_first;
  MPIR_All_communicators.comm_first = new_comm;

  ++MPIR_All_communicators.sequence_number;
}

/*+

MPIR_Comm_forget - forget a communicator which is going away
                   and bump the sequence number.
		   Do this as soon as the destruction begins, so
		   that the debugger doesn't see a partially destroyed
		   communicator.

+*/
void MPIR_Comm_forget( 
	struct MPIR_COMMUNICATOR *old_comm)
{
  struct MPIR_COMMUNICATOR * *p;

  for (p = &MPIR_All_communicators.comm_first; 
       *p;
       p = &((*p)->comm_next))
    {
      if (*p == old_comm)
	{
	  *p = old_comm->comm_next;
	  break;
	}
    }
  ++MPIR_All_communicators.sequence_number;
}
/* Init the collective ops functions.
 * Default to the ones MPIR provides.
 */

void MPIR_Comm_collops_init( 
	struct MPIR_COMMUNICATOR * comm,
	MPIR_COMM_TYPE comm_type)
{  
    comm->collops = (comm_type == MPIR_INTRA) ? MPIR_intra_collops :
                                                MPIR_inter_collops ;
    /* Here, we know that these collops are static, but it is still
     * useful to keep the ref count, because it avoids explicit checks
     * when we free them 
     */
    MPIR_REF_INCR(comm->collops);
}

/* Also used in comm_split.c */
#define MPIR_Table_color(table,i) table[(i)]
#define MPIR_Table_key(table,i)   table[((i)+size)]
#define MPIR_Table_next(table,i)  table[((i)+(2*size))]

/*+

MPIR_Sort_split_table - sort split table using a yuckie sort (YUCK!).  I'll
                        switch to a more efficient sort one of these days.

+*/
#define MPIR_EOTABLE -1
int MPIR_Sort_split_table ( 
	int size, 
	int rank, 
	int *table, 
	int *head, 
	int *list_size )
{
  int i, j, prev;
  int color = MPIR_Table_color(table,rank);
  
  /* Initialize head and list size */
  (*head)      = MPIR_EOTABLE;
  (*list_size) = 0;

  /* Sort out only colors == to my rank's color */
  for ( i=0; i<size; i++ ) {
    for (prev = MPIR_EOTABLE, j=(*head);
		 j != MPIR_EOTABLE; prev = j, j = MPIR_Table_next(table,j)) {
      if ( MPIR_Table_color(table,i) != color )
        continue;
      if ( (j==MPIR_EOTABLE) || (MPIR_Table_key(table,i)<MPIR_Table_key(table,j)) )
	break;
    }
    if ( MPIR_Table_color(table,i) == color) {
      (*list_size)++;
      MPIR_Table_next(table,i) = j;
      if (prev == MPIR_EOTABLE)
        (*head) = i;
      else
        MPIR_Table_next(table,prev) = i;
    }
  }
  return MPI_SUCCESS;
}

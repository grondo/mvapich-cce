/*
 *  $Id: context_util.c,v 1.2 2006/12/05 17:19:15 mamidala Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
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

/* context_util.c - Utilities used by the context chapter functions */

#include "mpiimpl.h"
#if (defined( _SMP_) && (defined(CH_GEN2) || defined(CH_SMP) || defined(CH_GEN2_UD) || defined(CH_PSM)))
extern int disable_shmem_allreduce;
extern int enable_shmem_collectives;
extern int disable_shmem_bcast;
int togled = 0;
#endif
/* Thanks to Jim Cownie for this code to handle small numbers of contexts */
#if defined(MPID_MAX_CONTEXT) && (MPID_MAX_CONTEXT < 256)
/* If there's a hard limit on the number of contexts available, then we 
 * attempt to re-use them.
 * 
 * We maintain a bit map and use logical or operations to locate free slots.
 * Note that the rest of the code ASSUMES that we allocate contiguous contexts
 * (which is rather a pain !).
 */
#define INTBITS (8*sizeof(int))

static unsigned int usedContextMap[ 1 + MPID_MAX_CONTEXT/INTBITS ] = 
   { (1<<MPIR_FIRST_FREE_CONTEXT)-1 };
static int lowestFreeBit = MPIR_FIRST_FREE_CONTEXT;

/* We write it this way for portability, and hope that the compiler
 * is clever enough to convert the divide and remainder operations into
 * >> and &. 
 */
#define set_bit(map, bit)   ( (map)[(bit)/INTBITS] |=  (1 << ((bit) % INTBITS)))
#define clear_bit(map, bit) ( (map)[(bit)/INTBITS] &= ~(1 << ((bit) % INTBITS)))
#define test_bit(map, bit)  (((map)[(bit)/INTBITS] &   (1 << ((bit) % INTBITS))) != 0)

static int findFree(unsigned int * map, const int number)
{
    int i;

    /* Look for a sufficient gap.
     * For simplicity we check a bit at a time,
     * however we do keep an eye on where the lowest free slot is
     * so that we can skip rapidly to somewhere reasonable.
     *
     * Using lowestFreeBit here is actually a bit tacky, because
     * it applies ONLY to our local usedContextMap. It is 
     * actually OK, because any of the maps we look in using this routine
     * are constructed by or'ing together our local map and something else. 
     * Therefore using the lowesFreeBit from our local map is always safe,
     * even if sub-optimal.
     */
    for (i=lowestFreeBit; i <= MPID_MAX_CONTEXT-number; i++)
    {
	int j;

	if (test_bit(map,i)) 
	    continue;

	for (j=1; j<number; j++)
	{
	    if (test_bit(map,i+j))
	    {
		i = i+j;  /* May as well skip forward here, for what it's worth */
		break;
	    }
	}

	if (j == number)
	    break; /* Found enough */
    }

    if (i > MPID_MAX_CONTEXT-number)
	/* Insufficient contiguous free contexts available */
	return -1;
    else
	return i;
}

/* 

MPIR_Context_alloc - allocate some number of contiguous contexts over given communicator

THREAD_SAFETY_ISSUE - this routine may currently be used in ways that is
not safe in a multithreaded environment.
 */
int MPIR_Context_alloc ( 
	struct MPIR_COMMUNICATOR *comm, 
	int num_contexts, 
	MPIR_CONTEXT *context )
{
  MPIR_CONTEXT result;
  int i;

  /* Lock for collective operation */
  MPID_THREAD_LOCK(comm->ADIctx,comm);

  /* Allocate contexts for intra-comms */
  if (comm->comm_type == MPIR_INTRA) 
  {
    unsigned int groupUsedMap[ 1 + MPID_MAX_CONTEXT/INTBITS ];

#if (defined( _SMP_) && (defined(CH_GEN2) || defined(CH_SMP) || defined(CH_GEN2_UD) || defined(CH_PSM)))
    if (enable_shmem_collectives){
        if (disable_shmem_allreduce != 1){    
            disable_shmem_allreduce = 1;
            togled = 1;
        }
    }
#endif

    /* Get the or of the masks everywhere */
    PMPI_Allreduce(usedContextMap, groupUsedMap, 1 + MPID_MAX_CONTEXT/INTBITS,
		  MPI_UNSIGNED,MPI_BOR,comm->self);
#if (defined( _SMP_) && (defined(CH_GEN2) || defined(CH_SMP) || defined(CH_GEN2_UD) || defined(CH_PSM)))
    if (enable_shmem_collectives){
        if (togled){
            disable_shmem_allreduce = 0;
            togled = 0;
        }
    }
#endif

    result = findFree(groupUsedMap, num_contexts);

  } else
  { /* Allocate contexts for inter-comms */
    MPI_Status   status;
    MPI_Comm     inter = comm->comm_coll;
    MPI_Comm     intra = inter->comm_coll;
    unsigned int localUsedMap[ 1 + MPID_MAX_CONTEXT/INTBITS ];
    unsigned int allUsedMap  [ 1 + MPID_MAX_CONTEXT/INTBITS ];
    int i;
    int rank;

#if (defined( _SMP_) && (defined(CH_GEN2) || defined(CH_SMP) || defined(CH_GEN2_UD) || defined(CH_PSM)))
    if (enable_shmem_collectives){
        if (disable_shmem_allreduce != 1){    
            disable_shmem_allreduce = 1;
            togled = 1;
        }
    }
#endif
    /* Find the allocated mask in the local group */
    PMPI_Allreduce(usedContextMap, localUsedMap, 1 + MPID_MAX_CONTEXT/INTBITS,
		  MPI_UNSIGNED, MPI_BOR, intra);
#if (defined( _SMP_) && (defined(CH_GEN2) || defined(CH_SMP) || defined(CH_GEN2_UD) || defined(CH_PSM)))
    if (enable_shmem_collectives){
        if (togled){
            disable_shmem_allreduce = 0;
	    togled = 0; 	
        }
    }
#endif

    MPIR_Comm_rank ( comm, &rank );
    if (rank == 0) 
    {
      /* Can't reduce in an inter communicator, (and we know there are only
       * two people participating), so simply swap the masks and do the or
       * by hand 
       */
      PMPI_Sendrecv(
	  localUsedMap,1+MPID_MAX_CONTEXT/INTBITS, MPI_UNSIGNED, 0, 0, 
	  allUsedMap,  1+MPID_MAX_CONTEXT/INTBITS, MPI_UNSIGNED, 0, 0, 
	  inter, &status);
      
      for (i=0; i<1+MPID_MAX_CONTEXT/INTBITS; i++)
	  allUsedMap[i] |= localUsedMap[i];

      result = findFree(allUsedMap, num_contexts);
    }
#if (defined(_SMP_) && defined(CH_GEN2)) || defined(CH_SMP)
    if (enable_shmem_collectives){
        if (disable_shmem_bcast != 1){    
            disable_shmem_bcast = 1;
            togled = 1;
        }
    }
#endif
    /* Leader give context info to local group */
    PMPI_Bcast (&result, 1, MPIR_CONTEXT_TYPE, 0, intra); 
#if (defined(_SMP_) && defined(CH_GEN2)) || defined(CH_SMP)
    if (enable_shmem_collectives){
        if (togled){
            disable_shmem_bcast = 0;
            togled = 0;
        }
    }
#endif
  }    

  if (result < 0)
  {
      MPID_THREAD_UNLOCK(comm->ADIctx,comm);
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_TOO_MANY_CONTEXTS,
				   (char *)0, 
				   (char *)0, "No more available contexts" );
      return MPIR_ERROR(comm, mpi_errno, (char*)0);
  }

  *context = result;

  /* Remember they're allocated */
  for (i=0; i<num_contexts; i++)
      set_bit(usedContextMap, result+i);

  /* Only need to change lowestFreeBit if we used the slot
   * it was pointing at.
   */
  if (result == lowestFreeBit)
  {
      /* Scan forward to find the new lowest slot */
      for (i=result+num_contexts; i <= MPID_MAX_CONTEXT; i++)
	  if (!test_bit(usedContextMap,i))
	      break;

      lowestFreeBit = i;
  }

  /* Lock for collective operation */
  MPID_THREAD_UNLOCK(comm->ADIctx,comm);
  return(MPI_SUCCESS); 
}

/*+

MPIR_Context_dealloc - deallocate previously allocated contexts 

+*/

/*
 * Should we treat this as a collective operation ?
 * I believe that the standard allows us to do so, and this would guard against
 * some potentially very obscure (user) bugs. [Along the lines of locally deleting 
 * a communicator, reallocating the context to a new one, and then having someone
 * in the original communicator send a message to us. This is a user problem, because
 * comm_free is specified as a collective operation, so this sequence is illegal].
 * However it's probably reasonable not to bother for now.
 */
int MPIR_Context_dealloc ( 
	struct MPIR_COMMUNICATOR *comm, 
	int num, 
	MPIR_CONTEXT context )
{
  while (num--)
      clear_bit(usedContextMap, context+num);

  if (context < lowestFreeBit)
      lowestFreeBit = context;

  return (MPI_SUCCESS);
}


#else   /* MPID_MAX_CONTEXT > 255, so we treat them as infinite (!) */
#define MPIR_MAX(a,b)  (((a)>(b))?(a):(b))

/* 

MPIR_Context_alloc - allocate some number of contexts over given communicator

 */
int MPIR_Context_alloc ( 
    struct MPIR_COMMUNICATOR *comm,
    int           num_contexts,
    MPIR_CONTEXT *context )
{
  static MPIR_CONTEXT high_context = MPIR_FIRST_FREE_CONTEXT;

  /* Lock for collective operation */
  MPID_THREAD_LOCK(comm->ADIctx,comm);

  /* Allocate contexts for intra-comms */
  if (comm->comm_type == MPIR_INTRA) {

#if (defined( _SMP_) && (defined(CH_GEN2) || defined(CH_SMP) || defined(CH_GEN2_UD) || defined(CH_PSM)))
    if (enable_shmem_collectives){
        if (disable_shmem_allreduce != 1){    
            disable_shmem_allreduce = 1;
            togled = 1;
        }
    }
#endif
    /* Find the highest context */
    PMPI_Allreduce(&high_context,context,1,MPIR_CONTEXT_TYPE,MPI_MAX,
		   comm->self);
#if (defined( _SMP_) && (defined(CH_GEN2) || defined(CH_SMP) || defined(CH_GEN2_UD) || defined(CH_PSM)))
    if (enable_shmem_collectives){
        if (togled){
            disable_shmem_allreduce = 0;
            togled = 0;
        }
    }
#endif
  }

  /* Allocate contexts for inter-comms */
  else {
    MPIR_CONTEXT rcontext;
    MPI_Status   status;
    struct MPIR_COMMUNICATOR *inter = comm->comm_coll;
    struct MPIR_COMMUNICATOR *intra = inter->comm_coll;
    int          rank;

#if (defined( _SMP_) && (defined(CH_GEN2) || defined(CH_SMP) || defined(CH_GEN2_UD) || defined(CH_PSM)))
    if (enable_shmem_collectives){
        if (disable_shmem_allreduce != 1){    
            disable_shmem_allreduce = 1;
            togled = 1;
        }
    }
#endif
    /* Find the highest context on the local group */
    PMPI_Allreduce(&high_context,context,1,MPIR_CONTEXT_TYPE,MPI_MAX,
		   intra->self);
#if (defined( _SMP_) && (defined(CH_GEN2) || defined(CH_SMP) || defined(CH_GEN2_UD) || defined(CH_PSM)))
    if (enable_shmem_collectives){
        if (togled){
            disable_shmem_allreduce = 0;
            togled = 0;
        }
    }
#endif

    MPIR_Comm_rank ( comm, &rank );
    if (rank == 0) {
      /* Leaders swap info */
      PMPI_Sendrecv(   context, 1, MPIR_CONTEXT_TYPE, 0, 0, 
		       &rcontext, 1, MPIR_CONTEXT_TYPE, 0, 0,
		       inter->self, &status);

      /* Update context to be the highest context */
      (*context) = MPIR_MAX((*context),rcontext);
    }

#if (defined(_SMP_) && defined(CH_GEN2)) || defined(CH_SMP)
    if (enable_shmem_collectives){
        if (disable_shmem_bcast != 1){    
            disable_shmem_bcast = 1;
            togled = 1;
        }
    }
#endif
    /* Leader give context info to everyone else */
    PMPI_Bcast (context, 1, MPIR_CONTEXT_TYPE, 0, intra->self); 
#if (defined(_SMP_) && defined(CH_GEN2)) || defined(CH_SMP)
    if (enable_shmem_collectives){
        if (togled){
            disable_shmem_bcast = 0;
            togled = 0;
        }
    }
#endif
  }

  /* Reset the highest context */
  high_context = (*context) + num_contexts;

  /* Lock for collective operation */
  MPID_THREAD_UNLOCK(comm->ADIctx,comm);

  return(MPI_SUCCESS); 
}

/*+

MPIR_Context_dealloc - deallocate previously allocated contexts 

+*/
/*ARGSUSED*/
int MPIR_Context_dealloc ( comm, num, context )
struct MPIR_COMMUNICATOR *comm;
int          num;
MPIR_CONTEXT context;
{
  /* This does nothing currently */
  return (MPI_SUCCESS);
}
#endif /* MPID_MAX_CONTEXT < 255 */

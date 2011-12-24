/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: flatten.c,v 1.18 2004/11/02 18:35:09 robl Exp $
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "adio_extern.h"
/* #ifdef MPISGI
#include "mpisgi2.h"
#endif */

void ADIOI_Optimize_flattened(ADIOI_Flatlist_node *flat_type);
void ADIOI_Flatten_subarray(int ndims,
			    int *array_of_sizes,
			    int *array_of_subsizes,
			    int *array_of_starts,
			    int order,
			    MPI_Datatype oldtype,
			    ADIOI_Flatlist_node *flat,
			    ADIO_Offset start_offset,
			    int *inout_index_p);
void ADIOI_Flatten_darray(int size,
			  int rank,
			  int ndims,
			  int array_of_gsizes[],
			  int array_of_distribs[],
			  int array_of_dargs[],
			  int array_of_psizes[],
			  int order,
			  MPI_Datatype oldtype,
			  ADIOI_Flatlist_node *flat,
			  ADIO_Offset start_offset,
			  int *inout_index_p);
void ADIOI_Flatten_copy_type(ADIOI_Flatlist_node *flat,
			     int old_type_start,
			     int old_type_end,
			     int new_type_start,
			     ADIO_Offset offset_adjustment);

/* darray helper functions */
#ifdef MPIIMPL_HAVE_MPI_COMBINER_DARRAY
static int index_of_type(int type_nr,
			 int dim_size,
			 int dim_rank,
			 int dim_ranks,
			 int k);
static int get_cyclic_k(int dim_size,
			int dim_ranks,
			int dist,
			int d_arg);
static void get_darray_position(int rank,
				int ranks,
				int ndims,
				int array_of_psizes[],
				int r[]);
static int local_types_in_dim(int dim_size,
			      int dim_rank,
			      int dim_ranks,
			      int k);
#endif

/* flatten datatype and add it to Flatlist */
void ADIOI_Flatten_datatype(MPI_Datatype datatype)
{
#ifdef HAVE_MPIR_TYPE_FLATTEN
    MPI_Aint flatten_idx;
#endif
    int curr_index=0, is_contig;
    ADIOI_Flatlist_node *flat, *prev=0;

    /* check if necessary to flatten. */
 
    /* is it entirely contiguous? */
    ADIOI_Datatype_iscontig(datatype, &is_contig);
    if (is_contig) return;

    /* has it already been flattened? */
    flat = ADIOI_Flatlist;
    while (flat) {
	if (flat->type == datatype) {
		return;
	}
	else {
	    prev = flat;
	    flat = flat->next;
	}
    }

    /* flatten and add to the list */
    flat = prev;
    flat->next = (ADIOI_Flatlist_node *)ADIOI_Malloc(sizeof(ADIOI_Flatlist_node));
    flat = flat->next;

    flat->type = datatype;
    flat->next = NULL;
    flat->blocklens = NULL;
    flat->indices = NULL;

    flat->count = ADIOI_Count_contiguous_blocks(datatype, &curr_index);
#if 0
    printf("cur_idx = %d\n", curr_index);
#endif
/*    FPRINTF(stderr, "%d\n", flat->count);*/

    if (flat->count) {
	flat->blocklens = (int *) ADIOI_Malloc(flat->count * sizeof(int));
	flat->indices = (ADIO_Offset *) ADIOI_Malloc(flat->count * \
						  sizeof(ADIO_Offset));
    }
	
    curr_index = 0;
#ifdef HAVE_MPIR_TYPE_FLATTEN
    flatten_idx = (MPI_Aint) flat->count;
    MPIR_Type_flatten(datatype, flat->indices, flat->blocklens, &flatten_idx);
#else
    ADIOI_Flatten(datatype, flat, 0, &curr_index);

    ADIOI_Optimize_flattened(flat);
#endif
/* debug */
#if 0
    {
	int i;
	FPRINTF(stderr, "blens: ");
	for (i=0; i<flat->count; i++) 
	    FPRINTF(stderr, "%d ", flat->blocklens[i]);
	FPRINTF(stderr, "\n\n");
	FPRINTF(stderr, "indices: ");
	for (i=0; i<flat->count; i++) 
	    FPRINTF(stderr, "%ld ", (long) flat->indices[i]);
	FPRINTF(stderr, "\n\n");
    }
#endif

}

/* ADIOI_Flatten()
 *
 * Assumption: input datatype is not a basic!!!!
 */
void ADIOI_Flatten(MPI_Datatype datatype, ADIOI_Flatlist_node *flat, 
		  ADIO_Offset st_offset, int *curr_index)  
{
    int i, j, k, m, n, num, basic_num, prev_index;
    int top_count, combiner, old_combiner, old_is_contig;
    int old_size, nints, nadds, ntypes, old_nints, old_nadds, old_ntypes;
    MPI_Aint old_extent;
    int *ints;
    MPI_Aint *adds;
    MPI_Datatype *types;

    MPI_Type_get_envelope(datatype, &nints, &nadds, &ntypes, &combiner);
    ints = (int *) ADIOI_Malloc((nints+1)*sizeof(int));
    adds = (MPI_Aint *) ADIOI_Malloc((nadds+1)*sizeof(MPI_Aint));
    types = (MPI_Datatype *) ADIOI_Malloc((ntypes+1)*sizeof(MPI_Datatype));
    MPI_Type_get_contents(datatype, nints, nadds, ntypes, ints, adds, types);

    switch (combiner) {
#ifdef MPIIMPL_HAVE_MPI_COMBINER_DUP
    case MPI_COMBINER_DUP:
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
			      &old_ntypes, &old_combiner); 
        ADIOI_Datatype_iscontig(types[0], &old_is_contig);
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
            ADIOI_Flatten(types[0], flat, st_offset, curr_index);
        break;
#endif
#ifdef MPIIMPL_HAVE_MPI_COMBINER_SUBARRAY
    case MPI_COMBINER_SUBARRAY:
	{
	    int dims = ints[0];
	    ADIOI_Flatten_subarray(dims,
				   &ints[1],        /* sizes */
				   &ints[dims+1],   /* subsizes */
				   &ints[2*dims+1], /* starts */
				   ints[3*dims+1],  /* order */
				   types[0],        /* type */
				   flat,
				   st_offset,
				   curr_index);
				   
	}
	break;
#endif
#ifdef MPIIMPL_HAVE_MPI_COMBINER_DARRAY
    case MPI_COMBINER_DARRAY:
	{
	    int dims = ints[2];
	    ADIOI_Flatten_darray(ints[0],         /* size */
				 ints[1],         /* rank */
				 dims,
				 &ints[3],        /* gsizes */
				 &ints[dims+3],   /* distribs */
				 &ints[2*dims+3], /* dargs */
				 &ints[3*dims+3], /* psizes */
				 ints[4*dims+3],  /* order */
				 types[0],
				 flat,
				 st_offset,
				 curr_index);
	}
	break;
#endif
    case MPI_COMBINER_CONTIGUOUS:
	top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
			      &old_ntypes, &old_combiner); 
        ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    ADIOI_Flatten(types[0], flat, st_offset, curr_index);

	if (prev_index == *curr_index) {
/* simplest case, made up of basic or contiguous types */
	    j = *curr_index;
	    flat->indices[j] = st_offset;
	    MPI_Type_size(types[0], &old_size);
	    flat->blocklens[j] = top_count * old_size;
	    (*curr_index)++;
	}
	else {
/* made up of noncontiguous derived types */
	    j = *curr_index;
	    num = *curr_index - prev_index;

/* The noncontiguous types have to be replicated count times */
	    MPI_Type_extent(types[0], &old_extent);
	    for (m=1; m<top_count; m++) {
		for (i=0; i<num; i++) {
		    flat->indices[j] = flat->indices[j-num] + old_extent;
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
	    }
	    *curr_index = j;
	}
	break;

    case MPI_COMBINER_VECTOR: 
	top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
			      &old_ntypes, &old_combiner); 
        ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    ADIOI_Flatten(types[0], flat, st_offset, curr_index);

	if (prev_index == *curr_index) {
/* simplest case, vector of basic or contiguous types */
	    j = *curr_index;
	    flat->indices[j] = st_offset;
	    MPI_Type_size(types[0], &old_size);
	    flat->blocklens[j] = ints[1] * old_size;
	    for (i=j+1; i<j+top_count; i++) {
		flat->indices[i] = flat->indices[i-1] + ints[2]*old_size;
		flat->blocklens[i] = flat->blocklens[j];
	    }
	    *curr_index = i;
	}
	else {
/* vector of noncontiguous derived types */

	    j = *curr_index;
	    num = *curr_index - prev_index;

/* The noncontiguous types have to be replicated blocklen times
   and then strided. Replicate the first one. */
	    MPI_Type_extent(types[0], &old_extent);
	    for (m=1; m<ints[1]; m++) {
		for (i=0; i<num; i++) {
		    flat->indices[j] = flat->indices[j-num] + old_extent;
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
	    }
	    *curr_index = j;

/* Now repeat with strides. */
	    num = *curr_index - prev_index;
	    for (i=1; i<top_count; i++) {
 		for (m=0; m<num; m++) {
		   flat->indices[j] =  flat->indices[j-num] + ints[2]
		       *old_extent;
		   flat->blocklens[j] = flat->blocklens[j-num];
		   j++;
		}
	    }
	    *curr_index = j;
	}
	break;

    case MPI_COMBINER_HVECTOR: 
	top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
			      &old_ntypes, &old_combiner); 
        ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    ADIOI_Flatten(types[0], flat, st_offset, curr_index);

	if (prev_index == *curr_index) {
/* simplest case, vector of basic or contiguous types */
	    j = *curr_index;
	    flat->indices[j] = st_offset;
	    MPI_Type_size(types[0], &old_size);
	    flat->blocklens[j] = ints[1] * old_size;
	    for (i=j+1; i<j+top_count; i++) {
		flat->indices[i] = flat->indices[i-1] + adds[0];
		flat->blocklens[i] = flat->blocklens[j];
	    }
	    *curr_index = i;
	}
	else {
/* vector of noncontiguous derived types */

	    j = *curr_index;
	    num = *curr_index - prev_index;

/* The noncontiguous types have to be replicated blocklen times
   and then strided. Replicate the first one. */
	    MPI_Type_extent(types[0], &old_extent);
	    for (m=1; m<ints[1]; m++) {
		for (i=0; i<num; i++) {
		    flat->indices[j] = flat->indices[j-num] + old_extent;
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
	    }
	    *curr_index = j;

/* Now repeat with strides. */
	    num = *curr_index - prev_index;
	    for (i=1; i<top_count; i++) {
 		for (m=0; m<num; m++) {
		   flat->indices[j] =  flat->indices[j-num] + adds[0];
		   flat->blocklens[j] = flat->blocklens[j-num];
		   j++;
		}
	    }
	    *curr_index = j;
	}
	break;

    case MPI_COMBINER_INDEXED: 
	top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
			      &old_ntypes, &old_combiner); 
        ADIOI_Datatype_iscontig(types[0], &old_is_contig);
	MPI_Type_extent(types[0], &old_extent);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    ADIOI_Flatten(types[0], flat,
			 st_offset+ints[top_count+1]*old_extent, curr_index);

	if (prev_index == *curr_index) {
/* simplest case, indexed type made up of basic or contiguous types */
	    j = *curr_index;
	    for (i=j; i<j+top_count; i++) {
		flat->indices[i] = st_offset + ints[top_count+1+i-j]*old_extent;
		flat->blocklens[i] = (int) (ints[1+i-j]*old_extent);
	    }
	    *curr_index = i;
	}
	else {
/* indexed type made up of noncontiguous derived types */

	    j = *curr_index;
	    num = *curr_index - prev_index;
	    basic_num = num;

/* The noncontiguous types have to be replicated blocklens[i] times
   and then strided. Replicate the first one. */
	    for (m=1; m<ints[1]; m++) {
		for (i=0; i<num; i++) {
		    flat->indices[j] = flat->indices[j-num] + old_extent;
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
	    }
	    *curr_index = j;

/* Now repeat with strides. */
	    for (i=1; i<top_count; i++) {
		num = *curr_index - prev_index;
		prev_index = *curr_index;
		for (m=0; m<basic_num; m++) {
		    flat->indices[j] = flat->indices[j-num] + 
                        (ints[top_count+1+i]-ints[top_count+i])*old_extent;
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
		*curr_index = j;
		for (m=1; m<ints[1+i]; m++) {
                    for (k=0; k<basic_num; k++) {
                        flat->indices[j] = flat->indices[j-basic_num] + old_extent;
                        flat->blocklens[j] = flat->blocklens[j-basic_num];
                        j++;
                    }
                }
		*curr_index = j;
	    }
	}
	break;

    case MPI_COMBINER_HINDEXED: 
	top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
			      &old_ntypes, &old_combiner); 
        ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    ADIOI_Flatten(types[0], flat, st_offset+adds[0], curr_index); 

	if (prev_index == *curr_index) {
/* simplest case, indexed type made up of basic or contiguous types */
	    j = *curr_index;
	    MPI_Type_size(types[0], &old_size);
	    for (i=j; i<j+top_count; i++) {
		flat->indices[i] = st_offset + adds[i-j];
		flat->blocklens[i] = ints[1+i-j]*old_size;
	    }
	    *curr_index = i;
	}
	else {
/* indexed type made up of noncontiguous derived types */

	    j = *curr_index;
	    num = *curr_index - prev_index;
	    basic_num = num;

/* The noncontiguous types have to be replicated blocklens[i] times
   and then strided. Replicate the first one. */
	    MPI_Type_extent(types[0], &old_extent);
	    for (m=1; m<ints[1]; m++) {
		for (i=0; i<num; i++) {
		    flat->indices[j] = flat->indices[j-num] + old_extent;
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
	    }
	    *curr_index = j;

/* Now repeat with strides. */
	    for (i=1; i<top_count; i++) {
		num = *curr_index - prev_index;
		prev_index = *curr_index;
		for (m=0; m<basic_num; m++) {
		    flat->indices[j] = flat->indices[j-num] + adds[i] - adds[i-1];
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
		*curr_index = j;
		for (m=1; m<ints[1+i]; m++) {
                    for (k=0; k<basic_num; k++) {
                        flat->indices[j] = flat->indices[j-basic_num] + old_extent;
                        flat->blocklens[j] = flat->blocklens[j-basic_num];
		    j++;
                    }
		}
		*curr_index = j;
	    }
	}
	break;

    case MPI_COMBINER_STRUCT: 
	top_count = ints[0];
	for (n=0; n<top_count; n++) {
	    MPI_Type_get_envelope(types[n], &old_nints, &old_nadds,
				  &old_ntypes, &old_combiner); 
            ADIOI_Datatype_iscontig(types[n], &old_is_contig);

	    prev_index = *curr_index;
            if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
		ADIOI_Flatten(types[n], flat, st_offset+adds[n], curr_index);

	    if (prev_index == *curr_index) {
/* simplest case, current type is basic or contiguous types */
		j = *curr_index;
		flat->indices[j] = st_offset + adds[n];
		MPI_Type_size(types[n], &old_size);
		flat->blocklens[j] = ints[1+n] * old_size;
		(*curr_index)++;
	    }
	    else {
/* current type made up of noncontiguous derived types */

		j = *curr_index;
		num = *curr_index - prev_index;

/* The current type has to be replicated blocklens[n] times */
		MPI_Type_extent(types[n], &old_extent);
		for (m=1; m<ints[1+n]; m++) {
		    for (i=0; i<num; i++) {
			flat->indices[j] = flat->indices[j-num] + old_extent;
			flat->blocklens[j] = flat->blocklens[j-num];
			j++;
		    }
		}
		*curr_index = j;
	    }
	}
 	break;

    default:
	/* TODO: FIXME (requires changing prototypes to return errors...) */
	FPRINTF(stderr, "Error: Unsupported datatype passed to ADIOI_Flatten\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

#ifndef MPISGI
/* There is a bug in SGI's impl. of MPI_Type_get_contents. It doesn't
   return new datatypes. Therefore no need to free. */
    for (i=0; i<ntypes; i++) {
 	MPI_Type_get_envelope(types[i], &old_nints, &old_nadds, &old_ntypes,
 			      &old_combiner);
 	if (old_combiner != MPI_COMBINER_NAMED) MPI_Type_free(types+i);
    }
#endif

    ADIOI_Free(ints);
    ADIOI_Free(adds);
    ADIOI_Free(types);

}

/********************************************************/

/* ADIOI_Count_contiguous_blocks
 *
 * Returns number of contiguous blocks in type, and also updates
 * curr_index to reflect the space for the additional blocks.
 *
 * ASSUMES THAT TYPE IS NOT A BASIC!!!
 */
int ADIOI_Count_contiguous_blocks(MPI_Datatype datatype, int *curr_index)
{
#ifdef HAVE_MPIR_TYPE_GET_CONTIG_BLOCKS
    /* MPICH2 can get us this value without all the envelope/contents calls */
    int blks;
    MPIR_Type_get_contig_blocks(datatype, &blks);
    *curr_index = blks;
    return blks;
#else
    int count=0, i, n, num, basic_num, prev_index;
    int top_count, combiner, old_combiner, old_is_contig;
    int nints, nadds, ntypes, old_nints, old_nadds, old_ntypes;
    int *ints;
    MPI_Aint *adds;
    MPI_Datatype *types;

    MPI_Type_get_envelope(datatype, &nints, &nadds, &ntypes, &combiner);
    ints = (int *) ADIOI_Malloc((nints+1)*sizeof(int));
    adds = (MPI_Aint *) ADIOI_Malloc((nadds+1)*sizeof(MPI_Aint));
    types = (MPI_Datatype *) ADIOI_Malloc((ntypes+1)*sizeof(MPI_Datatype));
    MPI_Type_get_contents(datatype, nints, nadds, ntypes, ints, adds, types);

    switch (combiner) {
#ifdef MPIIMPL_HAVE_MPI_COMBINER_DUP
    case MPI_COMBINER_DUP:
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
                              &old_ntypes, &old_combiner); 
	ADIOI_Datatype_iscontig(types[0], &old_is_contig);
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    count = ADIOI_Count_contiguous_blocks(types[0], curr_index);
	else {
		count = 1;
		(*curr_index)++;
	}
        break;
#endif
#ifdef MPIIMPL_HAVE_MPI_COMBINER_SUBARRAY
    case MPI_COMBINER_SUBARRAY:
	/* first get an upper bound (since we're not optimizing) on the
	 * number of blocks in the child type.
	 */
	MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
			      &old_ntypes, &old_combiner);
	ADIOI_Datatype_iscontig(types[0], &old_is_contig);


	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig)) {
	    count = ADIOI_Count_contiguous_blocks(types[0], curr_index);
	}
	else {
	    count = 1;
	    (*curr_index)++;
	}

	/* now multiply that by the number of types in the subarray.
	 *
	 * note: we could be smarter about this, because chances are
	 * our data is contiguous in the first dimension if the child
	 * type is contiguous.
	 *
	 * ints[0] - ndims
	 * ints[ndims+1..2*ndims] - subsizes
	 */
	top_count = 1; /* going to tally up # of types in here */
	for (i=0; i < ints[0]; i++) {
	    top_count *= ints[ints[0]+1+i];
	}

	if (top_count > 1) {
	    num = *curr_index - prev_index;
	    count *= top_count;
	    *curr_index += (top_count - 1) * num;
	}
	break;
#endif
#ifdef MPIIMPL_HAVE_MPI_COMBINER_DARRAY
    case MPI_COMBINER_DARRAY:
	{
	    int dims, k, *ranks;

	    MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
				  &old_ntypes, &old_combiner);
	    ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	    prev_index = *curr_index;
	    if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig)) {
		count = ADIOI_Count_contiguous_blocks(types[0], curr_index);
	    }
	    else {
		count = 1;
		(*curr_index)++;
	    }
	    
	    top_count = 1;
	    dims = ints[2];
	    ranks = ADIOI_Malloc(sizeof(int) * dims);
	    get_darray_position(ints[1],         /* rank */
				ints[0],         /* size */
				dims,
				&ints[3*dims+3], /* psizes */
				ranks);

	    for (i=0; i < dims; i++) {
		k = get_cyclic_k(ints[3+i],         /* gsize */
				 ints[3*dims+3+i],  /* psize */
				 ints[dims+3+i],    /* distrib */
				 ints[2*dims+3+i]); /* darg */

		top_count *= local_types_in_dim(ints[3+i],       /* gsize */
						ranks[i],        /* dim rank */
						ints[3*dims+3+i],/* psize */
						k);
	    }
	    ADIOI_Free(ranks);

	    if (top_count > 1) {
		num = *curr_index - prev_index;
		count *= top_count;
		*curr_index += (top_count - 1) * num;
	    }
	}
	break;
#endif
    case MPI_COMBINER_CONTIGUOUS:
        top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
                              &old_ntypes, &old_combiner); 
	ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    count = ADIOI_Count_contiguous_blocks(types[0], curr_index);
	else count = 1;

	if (prev_index == *curr_index) 
/* simplest case, made up of basic or contiguous types */
	    (*curr_index)++;
	else {
/* made up of noncontiguous derived types */
	    num = *curr_index - prev_index;
	    count *= top_count;
	    *curr_index += (top_count - 1)*num;
	}
	break;

    case MPI_COMBINER_VECTOR:
    case MPI_COMBINER_HVECTOR:
        top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
                              &old_ntypes, &old_combiner); 
	ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    count = ADIOI_Count_contiguous_blocks(types[0], curr_index);
	else count = 1;

	if (prev_index == *curr_index) {
/* simplest case, vector of basic or contiguous types */
	    count = top_count;
	    *curr_index += count;
	}
	else {
/* vector of noncontiguous derived types */
	    num = *curr_index - prev_index;

/* The noncontiguous types have to be replicated blocklen times
   and then strided. */
	    count *= ints[1] * top_count;

/* First one */
	    *curr_index += (ints[1] - 1)*num;

/* Now repeat with strides. */
	    num = *curr_index - prev_index;
	    *curr_index += (top_count - 1)*num;
	}
	break;

    case MPI_COMBINER_INDEXED: 
    case MPI_COMBINER_HINDEXED:
        top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
                              &old_ntypes, &old_combiner); 
	ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    count = ADIOI_Count_contiguous_blocks(types[0], curr_index);
	else count = 1;

	if (prev_index == *curr_index) {
/* simplest case, indexed type made up of basic or contiguous types */
	    count = top_count;
	    *curr_index += count;
	}
	else {
/* indexed type made up of noncontiguous derived types */
	    basic_num = *curr_index - prev_index;

/* The noncontiguous types have to be replicated blocklens[i] times
   and then strided. */
	    *curr_index += (ints[1]-1) * basic_num;
	    count *= ints[1];

/* Now repeat with strides. */
	    for (i=1; i<top_count; i++) {
		count += ints[1+i] * basic_num;
		*curr_index += ints[1+i] * basic_num;
	    }
	}
	break;

    case MPI_COMBINER_STRUCT: 
        top_count = ints[0];
	count = 0;
	for (n=0; n<top_count; n++) {
            MPI_Type_get_envelope(types[n], &old_nints, &old_nadds,
                                  &old_ntypes, &old_combiner); 
	    ADIOI_Datatype_iscontig(types[n], &old_is_contig);

	    prev_index = *curr_index;
	    if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    count += ADIOI_Count_contiguous_blocks(types[n], curr_index);

	    if (prev_index == *curr_index) {
/* simplest case, current type is basic or contiguous types */
		count++;
		(*curr_index)++;
	    }
	    else {
/* current type made up of noncontiguous derived types */
/* The current type has to be replicated blocklens[n] times */

		num = *curr_index - prev_index;
		count += (ints[1+n]-1)*num;
		(*curr_index) += (ints[1+n]-1)*num;
	    }
	}
	break;
    default:
	/* TODO: FIXME */
	FPRINTF(stderr, "Error: Unsupported datatype passed to ADIOI_Count_contiguous_blocks\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

#ifndef MPISGI
/* There is a bug in SGI's impl. of MPI_Type_get_contents. It doesn't
   return new datatypes. Therefore no need to free. */
    for (i=0; i<ntypes; i++) {
 	MPI_Type_get_envelope(types[i], &old_nints, &old_nadds, &old_ntypes,
 			      &old_combiner);
 	if (old_combiner != MPI_COMBINER_NAMED) MPI_Type_free(types+i);
    }
#endif

    ADIOI_Free(ints);
    ADIOI_Free(adds);
    ADIOI_Free(types);
    return count;
#endif /* HAVE_MPIR_TYPE_GET_CONTIG_BLOCKS */
}


/****************************************************************/

/* ADIOI_Optimize_flattened()
 *
 * Scans the blocks of a flattened type and merges adjacent blocks 
 * together, resulting in a shorter blocklist (and thus fewer
 * contiguous operations).
 *
 * Q: IS IT SAFE TO REMOVE THE 0-LENGTH BLOCKS TOO?
 */
void ADIOI_Optimize_flattened(ADIOI_Flatlist_node *flat_type)
{
    int i, j, opt_blocks;
    int *opt_blocklens;
    ADIO_Offset *opt_indices;
    
    opt_blocks = 1;
    
    /* save number of noncontiguous blocks in opt_blocks */
    for (i=0; i < (flat_type->count - 1); i++) {
        if ((flat_type->indices[i] + flat_type->blocklens[i] !=
	     flat_type->indices[i + 1]))
	    opt_blocks++;
    }

    /* if we can't reduce the number of blocks, quit now */
    if (opt_blocks == flat_type->count) return;

    opt_blocklens = (int *) ADIOI_Malloc(opt_blocks * sizeof(int));
    opt_indices = (ADIO_Offset *)ADIOI_Malloc(opt_blocks*sizeof(ADIO_Offset));

    /* fill in new blocklists */
    opt_blocklens[0] = flat_type->blocklens[0];
    opt_indices[0] = flat_type->indices[0];
    j = 0;
    for (i=0; i < (flat_type->count - 1); i++) {
	if ((flat_type->indices[i] + flat_type->blocklens[i] ==
	     flat_type->indices[i + 1]))
	    opt_blocklens[j] += flat_type->blocklens[i + 1];
	else {
	    j++;
	    opt_indices[j] = flat_type->indices[i + 1];
	    opt_blocklens[j] = flat_type->blocklens[i + 1];
	} 
    }
    flat_type->count = opt_blocks;
    ADIOI_Free(flat_type->blocklens);
    ADIOI_Free(flat_type->indices);
    flat_type->blocklens = opt_blocklens;
    flat_type->indices = opt_indices;
    return;
}

/****************************************************************/

void ADIOI_Delete_flattened(MPI_Datatype datatype)
{
    ADIOI_Flatlist_node *flat, *prev;

    prev = flat = ADIOI_Flatlist;
    while (flat && (flat->type != datatype)) {
	prev = flat;
	flat = flat->next;
    }
    if (flat) {
	prev->next = flat->next;
	if (flat->blocklens) ADIOI_Free(flat->blocklens);
	if (flat->indices) ADIOI_Free(flat->indices);
	ADIOI_Free(flat);
    }
}

/****************************************************************/

#ifdef MPIIMPL_HAVE_MPI_COMBINER_SUBARRAY
/* ADIOI_Flatten_subarray()
 *
 * ndims - number of dimensions in the array
 * array_of_sizes - dimensions of the array (one value per dim)
 * array_of_subsizes - dimensions of the subarray (one value per dim)
 * array_of_starts - starting offsets of the subarray (one value per dim)
 * order - MPI_ORDER_FORTRAN or MPI_ORDER_C
 * oldtype - type on which this subarray as built
 * flat - ...
 * start_offset - offset of this type to begin with
 * inout_index_p - as an input holds the count of indices (and blocklens)
 *                 that have already been used (also the index to the next
 *                 location to fill in!)
 */
void ADIOI_Flatten_subarray(int ndims,
			    int *array_of_sizes,
			    int *array_of_subsizes,
			    int *array_of_starts,
			    int order,
			    MPI_Datatype oldtype,
			    ADIOI_Flatlist_node *flat,
			    ADIO_Offset start_offset,
			    int *inout_index_p)
{
    int i, j, total_types, *dim_sz, flatten_start_offset,
	flatten_end_offset;
    int old_nints, old_nadds, old_ntypes, old_combiner;
    ADIO_Offset subarray_start_offset, type_offset, *dim_skipbytes;
    MPI_Aint oldtype_extent;

    MPI_Type_extent(oldtype, &oldtype_extent);

    /* TODO: optimization for 1-dimensional types -- treat like a contig */

    /* general case - make lots of copies of the offsets, adjusting 
     * the indices as necessary.
     */

    /* allocate space for temporary values */
    dim_sz = (int *) ADIOI_Malloc(sizeof(int)*(ndims));
    dim_skipbytes = (ADIO_Offset *) ADIOI_Malloc(sizeof(ADIO_Offset)*(ndims));

    /* get a count of the total number of types (of type oldtype) in
     * the subarray.
     */
    total_types = 1;
    for (i=0; i < ndims; i++) total_types *= array_of_subsizes[i];

    /* fill in the temporary values dim_sz and dim_skipbytes.
     *
     * these are used to calculate the starting offset for each instance
     * of oldtype in the subarray.  we precalculate these values so that
     * we can avoid calculating them for every instance.  this is all done
     * so that we can avoid a recursive algorithm here, which could be very
     * expensive if done for every dimension.
     *
     * finally, we're going to store these in C order regardless of how the
     * arrays were described in the first place.
     *
     * dim_sz is a value describing the number of types that go into a single
     * block in this dimension.  dim_skipbytes is the distance necessary to
     * move from one block to the next in this dimension.
     *
     * this is a little misleading, because actually dim_sz[ndims-1] is
     * always 1 and the dim_skipbytes[ndims-1] is always the extent...
     *
     * the dim_sz values are in terms of types, the dim_skipbytes are in
     * terms of bytes, and do *not* include the starting offset calculated
     * above.
     */

    /* priming the loop more or less */
    dim_sz[ndims-1] = 1;
    dim_skipbytes[ndims-1] = oldtype_extent;

    for (i=ndims-2; i >= 0; i--) {
	dim_sz[i] = dim_sz[i+1];
	dim_skipbytes[i] = dim_skipbytes[i+1];
	if (order == MPI_ORDER_FORTRAN) {
	    dim_sz[i] *= array_of_subsizes[ndims-2-i];
	    dim_skipbytes[i] *= array_of_sizes[ndims-2-i];
	}
	else {
	    dim_sz[i] *= array_of_subsizes[i+1];
	    dim_skipbytes[i] *= array_of_sizes[i+1];
	}
    }

    /* determine our starting offset for use later; this is a lot
     * easier to do now that we have the dim_skipbytes[] array from
     * above.
     */
    subarray_start_offset = 0;
    for (i=0; i < ndims; i++) {
	if (order == MPI_ORDER_FORTRAN) {
	    subarray_start_offset += array_of_starts[ndims-1-i]*dim_skipbytes[i];
	}
	else {
	    subarray_start_offset += array_of_starts[i]*dim_skipbytes[i];
	}
    }
    subarray_start_offset += start_offset; /* add in input parameter */

    /* flatten one of the type to get the offsets that we need;
     * really we need the starting offset to be right in order to
     * do this in-place.
     *
     * we save the offset to the first piece of the flattened type
     * so we can know what to make copies of in the next step.
     */
    flatten_start_offset = *inout_index_p;
    MPI_Type_get_envelope(oldtype, &old_nints, &old_nadds,
			  &old_ntypes, &old_combiner); 
    if (old_combiner != MPI_COMBINER_NAMED)
    {
	ADIOI_Flatten(oldtype, flat, subarray_start_offset, inout_index_p);
    }
    else {
	int oldtype_size;

	flat->indices[flatten_start_offset] = subarray_start_offset;
	MPI_Type_size(oldtype, &oldtype_size);
	flat->blocklens[flatten_start_offset] = oldtype_size;
	(*inout_index_p)++;
    }

    /* note this is really one larger than the end offset, but
     * that's what we want below.
     */
    flatten_end_offset = *inout_index_p;

    /* now we run through all the blocks, calculating their effective
     * offset and then making copies of all the regions for the type
     * for this instance.
     */
    for (i=1; i < total_types; i++) {
	int block_nr = i;
	type_offset = 0;

	for (j=0; j < ndims; j++) {
	    /* move through dimensions from least frequently changing
	     * to most frequently changing, calculating offset to get
	     * to a particular block, then reducing our block number
	     * so that we can correctly calculate based on the next 
	     * dimension's size.
	     */
	    int dim_index = block_nr / dim_sz[j];

	    if (dim_index) type_offset += dim_index * dim_skipbytes[j];
	    block_nr %= dim_sz[j];
	}

	/* do the copy */
	ADIOI_Flatten_copy_type(flat,
				flatten_start_offset,
				flatten_end_offset,
				flatten_start_offset + i * (flatten_end_offset - flatten_start_offset),
				type_offset);
    }

    /* free our temp space */
    ADIOI_Free(dim_sz);
    ADIOI_Free(dim_skipbytes);
    *inout_index_p = flatten_start_offset + total_types * (flatten_end_offset - flatten_start_offset);
}
#endif

/* ADIOI_Flatten_copy_type()
 * flat - pointer to flatlist node holding offset and lengths
 * start - starting index of src type in arrays
 * end - one larger than ending index of src type (makes loop clean)
 * offset_adjustment - amount to add to "indices" (offset) component
 *                     of each off/len pair copied
 */
void ADIOI_Flatten_copy_type(ADIOI_Flatlist_node *flat,
			     int old_type_start,
			     int old_type_end,
			     int new_type_start,
			     ADIO_Offset offset_adjustment)
{
    int i, out_index = new_type_start;

    for (i=old_type_start; i < old_type_end; i++) {
	flat->indices[out_index]   = flat->indices[i] + offset_adjustment;
	flat->blocklens[out_index] = flat->blocklens[i];
	out_index++;
    }
}

/****************************************************************/

#ifdef MPIIMPL_HAVE_MPI_COMBINER_DARRAY
/* ADIOI_Flatten_darray()
 *
 * size - number of processes across which darray is defined
 * rank - our rank in the group of processes
 * ndims - number of dimensions of darray type
 * gsizes - dimensions of the array in types (order varies)
 * distribs - type of dist. for each dimension (order varies)
 * dargs - argument to dist. for each dimension (order varies)
 * psizes - number of processes across which each dimension
 *          is split (always C order)
 * order - order of parameters (c or fortran)
 * oldtype - type on which this darray is built
 * flat - ...
 * start_offset - offset of this type to begin with
 * inout_index_p - count of indices already used on input, updated
 *                 for output
 *
 * The general approach is to convert everything into cyclic-k and process
 * it from there.
 */
void ADIOI_Flatten_darray(int size,
			  int rank,
			  int ndims,
			  int array_of_gsizes[],
			  int array_of_distribs[],
			  int array_of_dargs[],
			  int array_of_psizes[],
			  int order,
			  MPI_Datatype oldtype,
			  ADIOI_Flatlist_node *flat,
			  ADIO_Offset start_offset,
			  int *inout_index_p)
{
    int i, j, total_types, flatten_start_offset,
	flatten_end_offset, oldtype_nints, oldtype_nadds, oldtype_ntypes,
	oldtype_combiner, *dim_ranks, *dim_ks, *dim_localtypes;
    ADIO_Offset darray_start_offset, first_darray_offset;
    MPI_Aint oldtype_extent, *dim_skipbytes;

    MPI_Type_extent(oldtype, &oldtype_extent);

    dim_localtypes = ADIOI_Malloc(sizeof(int) * ndims);
    dim_skipbytes = ADIOI_Malloc(sizeof(MPI_Aint) * ndims);
    dim_ranks = (int *) ADIOI_Malloc(sizeof(int) * ndims);
    dim_ks = (int *) ADIOI_Malloc(sizeof(int) * ndims);

    /* fill in dim_ranks, C order (just like psizes) */
    get_darray_position(rank, size, ndims, array_of_psizes, dim_ranks);

    /* calculate all k values; store in same order as arrays */
    for (i=0; i < ndims; i++) {
	dim_ks[i] = get_cyclic_k(array_of_gsizes[i],
				 array_of_psizes[i],
				 array_of_distribs[i],
				 array_of_dargs[i]);
    }

    /* calculate total number of oldtypes in this type */
    total_types = 1;
    for (i=0; i < ndims; i++) {
	total_types *= local_types_in_dim(array_of_gsizes[i],
					  dim_ranks[i],
					  array_of_psizes[i],
					  dim_ks[i]);
    }

    /* fill in temporary values; these are just cached so we aren't
     * calculating them for every type instance.
     *
     * dim_localtypes holds the # of types this process has in the given
     * dimension.
     *
     * dim_skipbytes (in this function) is going to hold the distance
     * to skip to move from one type to the next in that dimension, in
     * bytes, in terms of the darray as a whole.  sort of like the stride
     * for a vector.
     * 
     * we keep this stuff in row-major (C) order -- least-frequently changing
     * first.
     */
    for (i=0; i < ndims; i++) {
	int idx = (order == MPI_ORDER_C) ? i : ndims-1-i;

	dim_localtypes[i] = local_types_in_dim(array_of_gsizes[idx],
					       dim_ranks[idx],
					       array_of_psizes[idx],
					       dim_ks[idx]);
    }

    dim_skipbytes[ndims-1] = oldtype_extent;
    for (i=ndims-2; i >= 0; i--) {
	int idx = (order == MPI_ORDER_C) ? i+1 : ndims-2-i;

	dim_skipbytes[i] = array_of_gsizes[idx] * dim_skipbytes[i+1];
    }

#if 0
    for (i=0; i < ndims; i++) {
	MPIU_dbg_printf("dim_skipbytes[%d] = %d, dim_localtypes[%d] = %d\n",
			i, (int) dim_skipbytes[i], i, dim_localtypes[i]);
    }
#endif


    /* determine starting offset */
    darray_start_offset = start_offset;
    first_darray_offset = 0;
    for (i=0; i < ndims; i++) {
	ADIO_Offset this_dim_off;
	int idx = (order == MPI_ORDER_C) ? i : ndims-1-i;

	this_dim_off = index_of_type(0,
				     array_of_gsizes[idx],
				     dim_ranks[i],
				     array_of_psizes[i],
				     dim_ks[idx]);

	this_dim_off *= (ADIO_Offset) dim_skipbytes[i];
	darray_start_offset += this_dim_off;
	first_darray_offset += this_dim_off;
    }
    
    /* flatten one of the type to get the offsets that we need;
     * we need an accurate starting offset to do this in-place.
     *
     * we save the starting offset so we can adjust when copying
     * later on.
     */
    flatten_start_offset = *inout_index_p;
    MPI_Type_get_envelope(oldtype,
			  &oldtype_nints,
			  &oldtype_nadds,
			  &oldtype_ntypes,
			  &oldtype_combiner);
    if (oldtype_combiner != MPI_COMBINER_NAMED) {
	ADIOI_Flatten(oldtype, flat, darray_start_offset, inout_index_p);
    }
    else {
	int oldtype_size;

	MPI_Type_size(oldtype, &oldtype_size);

	flat->indices[flatten_start_offset]   = darray_start_offset;
	flat->blocklens[flatten_start_offset] = oldtype_size;
	(*inout_index_p)++;
    }
    flatten_end_offset = *inout_index_p;

    /* now run through all the types, calculating the effective
     * offset and then making a copy of the flattened regions for the
     * type (and adjusting the offsets of them appropriately)
     */
    for (i=0; i < total_types; i++) {
	int block_nr = i;
	ADIO_Offset type_offset = 0;

	for (j=ndims-1; j >= 0; j--) {
	    ADIO_Offset dim_off;
	    int idx = (order == MPI_ORDER_C) ? j : ndims-1-j;
	    int dim_index = block_nr % dim_localtypes[j];

	    dim_off = index_of_type(dim_index,
				    array_of_gsizes[idx],
				    dim_ranks[j],
				    array_of_psizes[j],
				    dim_ks[idx]);


	    if (dim_off) type_offset += (ADIO_Offset) dim_off *
			     (ADIO_Offset) dim_skipbytes[j];
#if 0
	    {
		char s1[] = " ", s2[] = "  ", s3[] = "   ";
		MPIU_dbg_printf("%sindex of type %d (pass %d,%d) is %d; new offset = %d\n",
				(j == 0) ? s1 : ((j == 1) ? s2 : s3),
				dim_index, i, j, (int) dim_off, (int) type_offset);
	    }
#endif
	    block_nr /= dim_localtypes[j];
	}

	/* perform copy; noting in this case that the type offsets that
	 * we are calculating here are relative to the beginning of the
	 * darray as a whole, not relative to the first of our elements.
	 *
	 * because of that we have to subtract off the first_darray_offset
	 * in order to get the right offset adjustment.
	 */
	ADIOI_Flatten_copy_type(flat,
				flatten_start_offset,
				flatten_end_offset,
				flatten_start_offset + i * (flatten_end_offset - flatten_start_offset),
				type_offset - first_darray_offset);
    }
    
    /* free temp space */
    ADIOI_Free(dim_skipbytes);
    ADIOI_Free(dim_localtypes);
    ADIOI_Free(dim_ranks);
    ADIOI_Free(dim_ks);

    *inout_index_p = flatten_start_offset + total_types *
	(flatten_end_offset - flatten_start_offset);
}

/* darray processing helper functions */
static int index_of_type(int type_nr,
			 int dim_size,
			 int dim_rank,
			 int dim_ranks,
			 int k)
{
    int cycle, leftover, index;

    /* handle MPI_DISTRIBUTE_NONE case */
    if (k == 0) return type_nr;

    cycle = type_nr / k;
    leftover = type_nr % k;

    index = (dim_rank * k) + (dim_ranks * k * cycle) + leftover;

    return index;
}

static int get_cyclic_k(int dim_size,
			int dim_ranks,
			int dist,
			int d_arg)
{
    int k;

    /* calculate correct "k" if DFLT_DARG passed in */
    if (dist == MPI_DISTRIBUTE_NONE) return 0; /* indicates NONE */
    else if (d_arg == MPI_DISTRIBUTE_DFLT_DARG) {
	if (dist == MPI_DISTRIBUTE_BLOCK) {
	    k = (dim_size + dim_ranks - 1) / dim_ranks;
	}
	else {
	    k = 1;
	}
    }
    else {
	k = d_arg;
    }

    return k;
}

static int local_types_in_dim(int dim_size,
			      int dim_rank,
			      int dim_ranks,
			      int k)
{
    int count, n_blocks, n_types, leftover;

    if (k == 0) {
	/* indicates MPI_DISTRIBUTE_NONE */
	return dim_size;
    }

    /* blocks are regions of (up to k) types; this count
     * includes partials
     */
    n_blocks = (dim_size + k - 1) / k;

    /* count gets us a total # of blocks that the particular
     * rank gets, including possibly a partial block
     */
    count = n_blocks / dim_ranks;
    leftover = n_blocks - (count * dim_ranks);
    if (dim_rank < leftover) count++;

    n_types = count * k;

    /* subtract off the types that are missing from the final
     * partial block, if there is a partial and this rank is
     * the one that has it.
     */
    if ((dim_rank == leftover - 1) && (dim_size % k != 0)) {
	n_types -= k - (dim_size - ((dim_size / k) * k));
    }

    return n_types;
}

/* get_darray_position(rank, ranks, ndims, array_of_psizes, r[])
 *
 * Calculates the position of this process in the darray
 * given the rank of the process passed to the darray create
 * and the total number of processes also passed to the darray
 * create.
 *
 * Assumes that the array (r[]) has already been allocated.
 */
static void get_darray_position(int rank,
				int ranks,
				int ndims,
				int array_of_psizes[],
				int r[])
{
    int i;
    int t_rank = rank;
    int t_size = ranks;

    for (i = 0; i < ndims; i++) {
	t_size = t_size / array_of_psizes[i];
	r[i] = t_rank / t_size;
	t_rank = t_rank % t_size;
    }
} 
#endif

/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 *
 *   With contributions from Future Technologies Group, 
 *   Oak Ridge National Laboratory (http://ft.ornl.gov/)
 */

#include "adio.h"
#include "adio_extern.h"
/* #ifdef MPISGI
#include "mpisgi2.h"
#endif */

#if (defined(MPICH) || defined(MPICH2))
/* MPICH2 also provides this routine */
void MPIR_Datatype_iscontig(MPI_Datatype datatype, int *flag);

void ADIOI_Datatype_iscontig(MPI_Datatype datatype, int *flag)
{
    MPIR_Datatype_iscontig(datatype, flag);

    /* if it is MPICH2 and the datatype is reported as contigous,
       check if the true_lb is non-zero, and if so, mark the 
       datatype as noncontiguous */
#ifdef MPICH2
    if (*flag) {
        MPI_Aint true_extent, true_lb;
        
        PMPI_Type_get_true_extent(datatype, &true_lb, &true_extent);

        if (true_lb > 0)
            *flag = 0;
    }
#endif
}

#elif (defined(MPIHP) && defined(HAVE_MPI_INFO))
/* i.e. HPMPI 1.4 only */

int hpmp_dtiscontig(MPI_Datatype datatype);

void ADIOI_Datatype_iscontig(MPI_Datatype datatype, int *flag)
{
    *flag = hpmp_dtiscontig(datatype);
}

#elif (defined(MPISGI) && !defined(NO_MPI_SGI_type_is_contig))

int MPI_SGI_type_is_contig(MPI_Datatype datatype);

void ADIOI_Datatype_iscontig(MPI_Datatype datatype, int *flag)
{
    MPI_Aint displacement;
    MPI_Type_lb(datatype, &distplacement);

    /* SGI's MPI_SGI_type_is_contig() returns true for indexed
     * datatypes with holes at the beginning, which causes
     * problems with ROMIO's use of this function.
     */
    *flag = MPI_SGI_type_is_contig(datatype) && (displacement == 0);
}

#else

void ADIOI_Datatype_iscontig(MPI_Datatype datatype, int *flag)
{
    int nints, nadds, ntypes, combiner;
    int *ints, ni, na, nt, cb;
    MPI_Aint *adds;
    MPI_Datatype *types;

    MPI_Type_get_envelope(datatype, &nints, &nadds, &ntypes, &combiner);

    switch (combiner) {
    case MPI_COMBINER_NAMED:
	*flag = 1;
	break;
    case MPI_COMBINER_CONTIGUOUS:
	ints = (int *) ADIOI_Malloc((nints+1)*sizeof(int));
	adds = (MPI_Aint *) ADIOI_Malloc((nadds+1)*sizeof(MPI_Aint));
	types = (MPI_Datatype *) ADIOI_Malloc((ntypes+1)*sizeof(MPI_Datatype));
	MPI_Type_get_contents(datatype, nints, nadds, ntypes, ints,
			      adds, types); 
	ADIOI_Datatype_iscontig(types[0], flag);

#ifndef MPISGI
/* There is a bug in SGI's impl. of MPI_Type_get_contents. It doesn't
   return new datatypes. Therefore no need to free. */
	MPI_Type_get_envelope(types[0], &ni, &na, &nt, &cb);
	if (cb != MPI_COMBINER_NAMED) MPI_Type_free(types);
#endif

	ADIOI_Free(ints);
	ADIOI_Free(adds);
	ADIOI_Free(types);
	break;
    default:
	*flag = 0;
	break;
    }

    /* This function needs more work. It should check for contiguity 
       in other cases as well.*/
}
#endif

void ADIOI_Filetype_range_start(ADIO_File fd, ADIO_Offset offset, int file_ptr_type,
	int *start_index, int *start_ftype, int *start_offset, int *start_io_size)
{
    ADIOI_Flatlist_node *flat_file;
    ADIO_Offset disp, abs_off_in_filetype=0;
    MPI_Aint filetype_extent; 

    int i, st_io_size=0, st_index=0;
    int sum, n_etypes_in_filetype, size_in_filetype;
    int n_filetypes, etype_in_filetype;
    int flag, filetype_size, etype_size;

    flat_file = ADIOI_Flatlist;
    while (flat_file->type != fd->filetype) flat_file = flat_file->next;
    disp = fd->disp;

    MPI_Type_size(fd->filetype, &filetype_size);
    MPI_Type_extent(fd->filetype, &filetype_extent);
    etype_size = fd->etype_size;

    if (file_ptr_type == ADIO_INDIVIDUAL) {
	offset = fd->fp_ind; /* in bytes */
	n_filetypes = -1;
	flag = 0;
	while (!flag) {
	    n_filetypes++;
	    for (i=0; i<flat_file->count; i++) {
		if (disp + flat_file->indices[i] + 
		    (ADIO_Offset) n_filetypes*filetype_extent + flat_file->blocklens[i] 
			>= offset) {
		    st_index = i;
		    st_io_size = (int) (disp + flat_file->indices[i] + 
			    (ADIO_Offset) n_filetypes*filetype_extent
			     + flat_file->blocklens[i] - offset);
		    flag = 1;
		    break;
		}
	    }
	}
    } else {
	n_etypes_in_filetype = filetype_size/etype_size;
	n_filetypes = (int) (offset / n_etypes_in_filetype);
	etype_in_filetype = (int) (offset % n_etypes_in_filetype);
	size_in_filetype = etype_in_filetype * etype_size;

	sum = 0;
	for (i=0; i<flat_file->count; i++) {
	    sum += flat_file->blocklens[i];
	    if (sum > size_in_filetype) {
		st_index = i;
		st_io_size = sum - size_in_filetype;
		abs_off_in_filetype = flat_file->indices[i] +
		    size_in_filetype - (sum - flat_file->blocklens[i]);
		break;
	    }
	}

	/* abs. offset in bytes in the file */
	offset = disp + (ADIO_Offset) n_filetypes*filetype_extent + abs_off_in_filetype;
    }

    *start_index   = st_index;
    *start_io_size = st_io_size;
    *start_offset  = offset;
    *start_ftype   = n_filetypes;
}

void ADIOI_Filetype_range_iscontig(ADIO_File fd, ADIO_Offset offset, 
	int file_ptr_type, MPI_Datatype datatype, int count, int *flag)
{
    int srclen, datatype_size;
    int st_index, st_ftype, st_offset, st_io_size;

    MPI_Type_size(datatype, &datatype_size);
    srclen = datatype_size * count;

    ADIOI_Filetype_range_start(fd, offset, file_ptr_type,
	    &st_index, &st_ftype, &st_offset, &st_io_size);
    *flag = st_io_size >= srclen ? 1 : 0;
}


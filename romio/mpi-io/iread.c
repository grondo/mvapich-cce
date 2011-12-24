/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: iread.c,v 1.26 2005/02/18 00:39:07 robl Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_iread = PMPI_File_iread
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_iread MPI_File_iread
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_iread as PMPI_File_iread
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif
/*@
    MPI_File_iread - Nonblocking read using individual file pointer

Input Parameters:
. fh - file handle (handle)
. count - number of elements in buffer (nonnegative integer)
. datatype - datatype of each buffer element (handle)

Output Parameters:
. buf - initial address of buffer (choice)
. request - request object (handle)

.N fortran
@*/
#ifdef HAVE_MPI_GREQUEST
#include "mpiu_greq.h"

int MPI_File_iread(MPI_File mpi_fh, void *buf, int count, 
		   MPI_Datatype datatype, MPIO_Request *request)
{
	int error_code;
	MPI_Status *status;

        MPID_CS_ENTER();
        MPIR_Nest_incr();

	status = (MPI_Status *) ADIOI_Malloc(sizeof(MPI_Status));

	/* for now, no threads or anything fancy. 
	 * just call the blocking version */
	error_code = MPI_File_read(mpi_fh, buf, count, datatype, status); 
	/* ROMIO-1 doesn't do anything with status.MPI_ERROR */
	status->MPI_ERROR = error_code;

	/* kick off the request */
	MPI_Grequest_start(MPIU_Greq_query_fn, MPIU_Greq_free_fn, 
			   MPIU_Greq_cancel_fn, status, request);
	/* but we did all the work already */
	MPI_Grequest_complete(*request);

	/* passed the buck to the blocking version...*/

        MPIR_Nest_decr();
        MPID_CS_EXIT();
	return MPI_SUCCESS;
}
#else
int MPI_File_iread(MPI_File mpi_fh, void *buf, int count, 
                   MPI_Datatype datatype, MPIO_Request *request)
{
    int error_code;
    static char myname[] = "MPI_FILE_IREAD";
#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_START(fl_xmpi, BLKMPIFILEIREAD, TRDTSYSTEM, mpi_fh, datatype,
		  count);
#endif /* MPI_hpux */


    error_code = MPIOI_File_iread(mpi_fh, (MPI_Offset) 0, ADIO_INDIVIDUAL,
				  buf, count, datatype, myname, request);
    
#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, mpi_fh, datatype, count);
#endif /* MPI_hpux */


    return error_code;
}
#endif

#ifndef HAVE_MPI_GREQUEST
/* prevent multiple definitions of this routine */
#ifdef MPIO_BUILD_PROFILING
int MPIOI_File_iread(MPI_File mpi_fh,
		     MPI_Offset offset,
		     int file_ptr_type,
		     void *buf,
		     int count,
		     MPI_Datatype datatype,
		     char *myname,
		     MPIO_Request *request)
{
    int error_code, bufsize, buftype_is_contig, filetype_is_contig;
    int datatype_size;
    ADIO_Status status;
    ADIO_File fh;
    ADIO_Offset off;

    MPID_CS_ENTER();
    MPIR_Nest_incr();
    fh = MPIO_File_resolve(mpi_fh);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_FILE_HANDLE(fh, myname, error_code);
    MPIO_CHECK_COUNT(fh, count, myname, error_code);
    MPIO_CHECK_DATATYPE(fh, datatype, myname, error_code);

    if (file_ptr_type == ADIO_EXPLICIT_OFFSET && offset < 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "**iobadoffset", 0);
	error_code = MPIO_Err_return_file(fh, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    MPI_Type_size(datatype, &datatype_size);

    /* --BEGIN ERROR HANDLING-- */
    MPIO_CHECK_INTEGRAL_ETYPE(fh, count, datatype_size, myname, error_code);
    MPIO_CHECK_READABLE(fh, myname, error_code);
    MPIO_CHECK_NOT_SEQUENTIAL_MODE(fh, myname, error_code);
    /* --END ERROR HANDLING-- */

    ADIOI_Datatype_iscontig(datatype, &buftype_is_contig);
    ADIOI_Datatype_iscontig(fh->filetype, &filetype_is_contig);

    ADIOI_TEST_DEFERRED(fh, myname, &error_code);

    if (buftype_is_contig && filetype_is_contig) {
	/* convert count and offset to bytes */
	bufsize = datatype_size * count;

	if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
	    off = fh->disp + fh->etype_size * offset;
	}
	else {
	    off = fh->fp_ind;
	}

        if (!(fh->atomicity))
	    ADIO_IreadContig(fh, buf, count, datatype, file_ptr_type,
			off, request, &error_code); 
        else {
            /* to maintain strict atomicity semantics with other concurrent
              operations, lock (exclusive) and call blocking routine */

            *request = ADIOI_Malloc_request();
            (*request)->optype = ADIOI_READ;
            (*request)->fd = fh;
            (*request)->datatype = datatype;
            (*request)->queued = 0;
	    (*request)->handle = 0;

            if ((fh->file_system != ADIO_PIOFS) && 
              (fh->file_system != ADIO_NFS) && (fh->file_system != ADIO_PVFS)
	      && (fh->file_system != ADIO_PVFS2))
	    {
                ADIOI_WRITE_LOCK(fh, off, SEEK_SET, bufsize);
	    }

            ADIO_ReadContig(fh, buf, count, datatype, file_ptr_type, 
			    off, &status, &error_code);

            if ((fh->file_system != ADIO_PIOFS) && 
               (fh->file_system != ADIO_NFS) && (fh->file_system != ADIO_PVFS)
	       && (fh->file_system != ADIO_PVFS2))
	    {
                ADIOI_UNLOCK(fh, off, SEEK_SET, bufsize);
	    }

            fh->async_count++;
            /* status info. must be linked to the request structure, so that it
               can be accessed later from a wait */
        }
    }
    else ADIO_IreadStrided(fh, buf, count, datatype, file_ptr_type,
			   offset, request, &error_code); 
fn_exit:
    MPID_CS_EXIT();
    MPIR_Nest_decr();

    return error_code;
}
#endif
#endif

/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: iread_at.c,v 1.23 2005/02/18 00:39:07 robl Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_iread_at = PMPI_File_iread_at
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_iread_at MPI_File_iread_at
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_iread_at as PMPI_File_iread_at
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
    MPI_File_iread_at - Nonblocking read using explict offset

Input Parameters:
. fh - file handle (handle)
. offset - file offset (nonnegative integer)
. count - number of elements in buffer (nonnegative integer)
. datatype - datatype of each buffer element (handle)

Output Parameters:
. buf - initial address of buffer (choice)
. request - request object (handle)

.N fortran
@*/
#ifdef HAVE_MPI_GREQUEST
#include "mpiu_greq.h"
int MPI_File_iread_at(MPI_File mpi_fh, MPI_Offset offset, void *buf,
                      int count, MPI_Datatype datatype, 
                      MPIO_Request *request)
{
	int error_code;
	MPI_Status *status;

        MPID_CS_ENTER();
        MPIR_Nest_incr();

	status = (MPI_Status *) ADIOI_Malloc(sizeof(MPI_Status));

	/* for now, no threads or anything fancy. 
	 * just call the blocking version */
	error_code = MPI_File_read_at(mpi_fh, offset, buf, count, datatype,
				      status); 
	/* ROMIO-1 doesn't do anything with status.MPI_ERROR */
	status->MPI_ERROR = error_code;

	/* kick off the request */
	MPI_Grequest_start(MPIU_Greq_query_fn, MPIU_Greq_free_fn, 
			   MPIU_Greq_cancel_fn, status, request);
	/* but we did all the work already */
	MPI_Grequest_complete(*request);

        MPIR_Nest_decr();
        MPID_CS_EXIT();

	/* passed the buck to the blocking version...*/
	return MPI_SUCCESS;
}
#else
int MPI_File_iread_at(MPI_File mpi_fh, MPI_Offset offset, void *buf,
                      int count, MPI_Datatype datatype, 
                      MPIO_Request *request)
{
    int error_code;
    static char myname[] = "MPI_FILE_IREAD_AT";

#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_START(fl_xmpi, BLKMPIFILEIREADAT, TRDTSYSTEM, mpi_fh, datatype,
		  count);
#endif /* MPI_hpux */


    error_code = MPIOI_File_iread(mpi_fh, offset, ADIO_EXPLICIT_OFFSET, buf,
				  count, datatype, myname, request);

#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, mpi_fh, datatype, count);
#endif /* MPI_hpux */

    return error_code;
}
#endif

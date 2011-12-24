/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_ntfs.h"

void ADIOI_NTFS_IwriteContig(ADIO_File fd, void *buf, int count, 
			     MPI_Datatype datatype, int file_ptr_type,
			     ADIO_Offset offset, ADIO_Request *request,
			     int *error_code)  
{
    int len, typesize;
    int err=FALSE;
    static char myname[] = "ADIOI_NTFS_IWRITECONTIG";

    *request = ADIOI_Malloc_request();
    (*request)->optype = ADIOI_WRITE;
    (*request)->fd = fd;
    (*request)->datatype = datatype;

    MPI_Type_size(datatype, &typesize);
    len = count * typesize;

    if (file_ptr_type == ADIO_INDIVIDUAL) offset = fd->fp_ind;
    err = ADIOI_NTFS_aio(fd, buf, len, offset, 1, &((*request)->handle));
    if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind += len;

    (*request)->queued = 1;
    ADIOI_Add_req_to_list(request);

    if (err == FALSE) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
	return;
    }
    else *error_code = MPI_SUCCESS;

    fd->fp_sys_posn = -1;   /* set it to null. */
    fd->async_count++;
}


/* This function is for implementation convenience. It is not user-visible.
 * If wr==1 write, wr==0 read.
 *
 * Returns TRUE on success, FALSE on failure.  Error code construction is
 * handled above this function.
 */
int ADIOI_NTFS_aio(ADIO_File fd, void *buf, int len, ADIO_Offset offset,
		   int wr, void *handle)
{
    DWORD dwNumWritten=0, dwNumRead=0;
    BOOL ret_val = FALSE;
    FDTYPE fd_sys;

    OVERLAPPED *pOvl;

    fd_sys = fd->fd_sys;

    pOvl = (OVERLAPPED *) ADIOI_Calloc(sizeof(OVERLAPPED), 1);
    pOvl->hEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
    pOvl->Offset = DWORDLOW(offset);
    pOvl->OffsetHigh = DWORDHIGH(offset);

    if (wr)
    {
	ret_val = WriteFile(fd_sys, buf, len, &dwNumWritten, pOvl);
	/*
	ret_val = WriteFile(fd_sys, buf, len, &dwNumWritten, NULL);
	if (ret_val && dwNumWritten) printf("written immediately: %d\n", dwNumWritten);
	*/
    }
    else
    {
	ret_val = ReadFile(fd_sys, buf, len, &dwNumRead, pOvl);
	/*ret_val = ReadFile(fd_sys, buf, len, &dwNumRead, NULL);*/
    }

    if (ret_val == FALSE) 
    {
	errno = GetLastError();
	if (errno != ERROR_IO_PENDING)
	{
	    if (wr) {
		FPRINTF(stderr,
			"WriteFile error (%d): len %d, dwNumWritten %d\n",
			errno, len, dwNumWritten);
	    }
	    else {
		FPRINTF(stderr,
			"ReadFile error (%d): len %d, dwNumRead %d\n",
			errno, len, dwNumRead);
	    }
	    return FALSE;
	}
	ret_val = TRUE;
    }

    *((OVERLAPPED **) handle) = pOvl;

    return ret_val;
}

/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_ntfs.h"

void ADIOI_NTFS_WriteContig(ADIO_File fd, void *buf, int count, 
			    MPI_Datatype datatype, int file_ptr_type,
			    ADIO_Offset offset, ADIO_Status *status,
			    int *error_code)
{
    DWORD dwTemp;
    DWORD dwNumWritten = 0;
    int err=-1, datatype_size, len;
    static char myname[] = "ADIOI_NTFS_WRITECONTIG";

    MPI_Type_size(datatype, &datatype_size);
    len = datatype_size * count;

    if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
	if (fd->fp_sys_posn != offset)
	{
	    dwTemp = DWORDHIGH(offset);
	    SetFilePointer(fd->fd_sys, DWORDLOW(offset), &dwTemp, FILE_BEGIN);
	}
	err = WriteFile(fd->fd_sys, buf, len, &dwNumWritten, NULL);
	fd->fp_sys_posn = offset + dwNumWritten;
	/* individual file pointer not updated */        
    }
    else { /* write from curr. location of ind. file pointer */
	if (fd->fp_sys_posn != fd->fp_ind)
	{
	    dwTemp = DWORDHIGH(fd->fp_ind);
	    SetFilePointer(fd->fd_sys, DWORDLOW(fd->fp_ind), &dwTemp,
			   FILE_BEGIN);
	}
	err = WriteFile(fd->fd_sys, buf, len, &dwNumWritten, NULL);
	fd->fp_ind = fd->fp_ind + dwNumWritten;
	fd->fp_sys_posn = fd->fp_ind;
    }

#ifdef HAVE_STATUS_SET_BYTES
    if (err != FALSE) MPIR_Status_set_bytes(status, datatype, dwNumWritten);
#endif

    if (err == FALSE) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
	return;
    }
    else *error_code = MPI_SUCCESS;
}

/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 *
 *   With contributions from Future Technologies Group, 
 *   Oak Ridge National Laboratory (http://ft.ornl.gov/)
 */

#include "ad_lustre.h"

static void ADIOI_LUSTRE_IOContig(ADIO_File fd, void *buf, int count, 
                   MPI_Datatype datatype, int file_ptr_type,
	           ADIO_Offset offset, ADIO_Status *status, 
		   int io_mode, int *error_code);

static void ADIOI_LUSTRE_IOContig(ADIO_File fd, void *buf, int count, 
                   MPI_Datatype datatype, int file_ptr_type,
	           ADIO_Offset offset, ADIO_Status *status, 
		   int io_mode, int *error_code)
{
    int err=-1, datatype_size, len;
#if defined(MPICH2) || !defined(PRINT_ERR_MSG)
    static char myname[] = "ADIOI_LUSTRE_IOCONTIG";
#endif

    MPI_Type_size(datatype, &datatype_size);
    len = datatype_size * count;

    if (file_ptr_type == ADIO_INDIVIDUAL) {
	offset = fd->fp_ind;
    }

    if (fd->fp_sys_posn != offset) {
	err = lseek(fd->fd_sys, offset, SEEK_SET);
	if (err == -1) goto ioerr;
    }
    
    if (io_mode)
	err = write(fd->fd_sys, buf, len);
    else 
	err = read(fd->fd_sys, buf, len);

    if (err == -1) goto ioerr;
    fd->fp_sys_posn = offset + err;

    if (file_ptr_type == ADIO_INDIVIDUAL) {
	fd->fp_ind += err; 
    }

#ifdef HAVE_STATUS_SET_BYTES
    if (status) MPIR_Status_set_bytes(status, datatype, err);
#endif
    *error_code = MPI_SUCCESS;

ioerr:
    /* --BEGIN ERROR HANDLING-- */
    if (err == -1) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS,
					   MPIR_ERR_RECOVERABLE,
					   myname, __LINE__,
					   MPI_ERR_IO, "**io",
					   "**io %s", strerror(errno));
	fd->fp_sys_posn = -1;
	return;
    }
    /* --END ERROR HANDLING-- */
}

void ADIOI_LUSTRE_WriteContig(ADIO_File fd, void *buf, int count, 
                   MPI_Datatype datatype, int file_ptr_type,
	           ADIO_Offset offset, ADIO_Status *status, int *error_code)
{
    ADIOI_LUSTRE_IOContig(fd, buf, count, 
                   datatype, file_ptr_type,
	           offset, status, 1, error_code);
}

void ADIOI_LUSTRE_ReadContig(ADIO_File fd, void *buf, int count, 
                   MPI_Datatype datatype, int file_ptr_type,
	           ADIO_Offset offset, ADIO_Status *status, int *error_code)
{
    ADIOI_LUSTRE_IOContig(fd, buf, count, 
                   datatype, file_ptr_type,
	           offset, status, 0, error_code);
}

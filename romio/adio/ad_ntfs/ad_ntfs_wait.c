/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_ntfs.h"

void ADIOI_NTFS_ReadComplete(ADIO_Request *request, ADIO_Status *status,
			     int *error_code)  
{
    DWORD ret_val;
    static char myname[] = "ADIOI_NTFS_READCOMPLETE";

    if (*request == ADIO_REQUEST_NULL) {
	*error_code = MPI_SUCCESS;
	return;
    }
    
    if ((*request)->queued) {
	ret_val = GetOverlappedResult((*request)->fd, (*request)->handle,
				      &(*request)->nbytes, TRUE);

	/* Is this a busy wait on the aio handle?
	do {
	    err = aio_suspend((const aiocb_t **) &((*request)->handle), 1, 0);
	} while ((err == -1) && (errno == EINTR));
	*/

	if (!ret_val)
	    (*request)->nbytes = -1;

	if (ret_val == FALSE) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	    return;
	}
	else *error_code = MPI_SUCCESS;
    } /* if ((*request)->queued) ... */
    else *error_code = MPI_SUCCESS;
#ifdef HAVE_STATUS_SET_BYTES
    if ((*request)->nbytes != -1)
	MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif

    if ((*request)->queued != -1) {

	/* queued = -1 is an internal hack used when the request must
	   be completed, but the request object should not be
	   freed. This is used in ADIOI_Complete_async, because the user
	   will call MPI_Wait later, which would require status to
	   be filled. Ugly but works. queued = -1 should be used only
	   in ADIOI_Complete_async. 
           This should not affect the user in any way. */

	/* if request is still queued in the system, it is also there
           on ADIOI_Async_list. Delete it from there. */
	if ((*request)->queued) ADIOI_Del_req_from_list(request);

	(*request)->fd->async_count--;
	if ((*request)->handle)
	{
	    CloseHandle(((OVERLAPPED*)((*request)->handle))->hEvent);
	    ADIOI_Free((*request)->handle);
	}
	ADIOI_Free_request((ADIOI_Req_node *) (*request));
	*request = ADIO_REQUEST_NULL;
    }

}


void ADIOI_NTFS_WriteComplete(ADIO_Request *request, ADIO_Status *status,
			      int *error_code)
{
    ADIOI_NTFS_ReadComplete(request, status, error_code);
}

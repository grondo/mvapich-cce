/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: ad_sfs_done.c,v 1.4 2002/10/24 17:01:01 gropp Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_sfs.h"

int ADIOI_SFS_ReadDone(ADIO_Request *request, ADIO_Status *status, int *error_code)  
{
    if (*request != ADIO_REQUEST_NULL) {
#ifdef HAVE_STATUS_SET_BYTES
	MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif
	(*request)->fd->async_count--;
	ADIOI_Free_request((ADIOI_Req_node *) (*request));
	*request = ADIO_REQUEST_NULL;
    }

    *error_code = MPI_SUCCESS;
    return 1;
}


int ADIOI_SFS_WriteDone(ADIO_Request *request, ADIO_Status *status, int *error_code)  
{
    return ADIOI_SFS_ReadDone(request, status, error_code);
} 

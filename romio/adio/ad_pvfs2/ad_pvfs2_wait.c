/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: ad_pvfs2_wait.c,v 1.2 2003/06/27 23:28:29 robl Exp $
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pvfs2.h"

void ADIOI_PVFS2_ReadComplete(ADIO_Request *request, ADIO_Status *status, int *error_code)  
{
    if (*request == ADIO_REQUEST_NULL) {
        *error_code = MPI_SUCCESS;
        return;
    }

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif
    (*request)->fd->async_count--;
    ADIOI_Free_request((ADIOI_Req_node *) (*request));
    *request = ADIO_REQUEST_NULL;
    *error_code = MPI_SUCCESS;
}

void ADIOI_PVFS2_WriteComplete(ADIO_Request *request, ADIO_Status *status, int *error_code)  
{
    ADIOI_PVFS2_ReadComplete(request, status, error_code);
}

/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: ad_done_fake.c,v 1.1 2005/09/01 21:31:48 surs Exp $    
 *
 *   Copyright (C) 2004 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"

/* Generic implementation of ReadDone/WriteDone simply sets the
 * bytes field in the status structure and frees the request.
 *
 * Same function is used for both reads and writes.
 */
int ADIOI_FAKE_IODone(ADIO_Request *request, ADIO_Status *status,
		     int *error_code)
{
    if (*request != ADIO_REQUEST_NULL) {
#ifdef HAVE_STATUS_SET_BYTES
	MPIR_Status_set_bytes(status, (*request)->datatype,
			      (*request)->nbytes);
#endif
	(*request)->fd->async_count--;
	ADIOI_Free_request((ADIOI_Req_node *) (*request));
	*request = ADIO_REQUEST_NULL;
    }

    *error_code = MPI_SUCCESS;
    return 1;
}

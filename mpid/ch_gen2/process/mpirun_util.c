/* Copyright (c) 2002-2010, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH in the top level MPICH directory.
 *
 */

#include "mpirun_util.h"
#include <errno.h>
#include <string.h>

/*
 * ptr must be suitable for a call to realloc
 */
char * vedit_str(char * const ptr, const char * format, va_list args) {
    va_list ap;
    int size;
    char * str;

    va_copy(ap, args);
    size = vsnprintf(NULL, 0, format, ap);
    va_end(ap);

    if(size++ < 0) return NULL;

    str = realloc(ptr, sizeof(char) * size);

    if(!str) {
	perror("vedit_str [realloc]");
	exit(EXIT_FAILURE);
    }

    va_copy(ap, args);
    size = vsnprintf(str, size, format, ap);
    va_end(ap);

    if(size < 0) return NULL;

    return str;
}

/*
 * ptr must be suitable for a call to realloc
 */
char * edit_str(char * const ptr, char const * const format, ...) {
    va_list ap;
    char * str;

    va_start(ap, format);
    str = vedit_str(ptr, format, ap);
    va_end(ap);
    
    return str;
}

char * mkstr(char const * const format, ...) {
    va_list ap;
    char * str;

    va_start(ap, format);
    str = vedit_str(NULL, format, ap);
    va_end(ap);

    return str;
}

/*
 * ptr & suffix must be dynamically allocated
 */
char * append_str(char * ptr, char * const suffix) {
    va_list ap;

    ptr = realloc(ptr, sizeof(char) * (strlen(ptr) + strlen(suffix) + 1));

    if(!ptr) {
	perror("append_str [realloc]");
	exit(EXIT_FAILURE);
    }

    strcat(ptr, suffix);
    free(suffix);

    return ptr;
}


int read_socket(int socket, void * buffer, size_t bytes) {
    char * data = buffer;
    ssize_t rv;

    while(bytes != 0) {
	if((rv = read(socket, data, bytes)) == -1) {
	    switch(errno) {
		case EINTR:
		case EAGAIN:
		    continue;
		default:
		    perror("read");
		    return -1;
	    }
	}

	data += rv;
	bytes -= rv;
    }

    return 0;
}

int write_socket(int socket, void * buffer, size_t bytes) {
    char * data = buffer;
    ssize_t rv;

    while(bytes != 0) {
	if((rv = write(socket, data, bytes)) == -1) {
	    switch(errno) {
		case EINTR:
		case EAGAIN:
		    continue;
		default:
		    perror("write");
		    return -1;
	    }
	}

	data += rv;
	bytes -= rv;
    }

    return 0;
}

/* vi:set sw=4 sts=4 tw=80 */

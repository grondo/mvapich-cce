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

#ifndef DISABLE_PTMALLOC

#include "mem_hooks.h"
#include "dreg.h"

#ifndef DISABLE_MUNMAP_HOOK
#include <dlfcn.h>

static void set_real_munmap_ptr()
{
    char *dlerror_str;
    void *ptr_dlsym;

    ptr_dlsym = dlsym(RTLD_NEXT, "munmap");
    dlerror_str = dlerror();

    if(NULL != dlerror_str) {
        fprintf(stderr,"Error resolving munmap (%s)\n",
                dlerror_str);
    }       

    mvapich_minfo.munmap = ptr_dlsym;
}

#endif

void mvapich_mem_unhook(void *ptr, size_t size)
{
    if((size > 0) && 
            !mvapich_minfo.is_mem_hook_finalized) {
        find_and_free_dregs_inside(ptr, size);
    }
}

int mvapich_minit()
{
    int ret = 0;
    void *ptr_malloc = NULL;
    void *ptr_calloc = NULL;
    void *ptr_valloc = NULL;
    void *ptr_realloc = NULL;
    void *ptr_memalign = NULL;

    memset(&mvapich_minfo, 0, sizeof(mvapich_malloc_info_t));

    ptr_malloc = malloc(64);
    ptr_calloc = calloc(64, 1);
    ptr_realloc = realloc(ptr_malloc, 64);
    ptr_valloc = valloc(64);
    ptr_memalign = memalign(64, 64);


    free(ptr_calloc);
    free(ptr_valloc);
    free(ptr_memalign);

    /* ptr_realloc already contains the
     * memory allocated by malloc */
    free(ptr_realloc);

    if(!(mvapich_minfo.is_our_malloc &&
            mvapich_minfo.is_our_calloc &&
            mvapich_minfo.is_our_realloc &&
            mvapich_minfo.is_our_valloc &&
            mvapich_minfo.is_our_memalign &&
            mvapich_minfo.is_our_free)) {
        return 1;
    }

#ifndef DISABLE_MUNMAP_HOOK
    dlerror(); /* Clear the error string */
    set_real_munmap_ptr();
#endif

    return ret;
}

void mvapich_mfin()
{
    mvapich_minfo.is_mem_hook_finalized = 1;
}

#ifndef DISABLE_MUNMAP_HOOK

int mvapich_munmap(void *buf, size_t len)
{
    if(!mvapich_minfo.munmap) {
        set_real_munmap_ptr();
    }

    if(!mvapich_minfo.is_mem_hook_finalized) {
        mvapich_mem_unhook(buf, len);
    }

    return mvapich_minfo.munmap(buf, len);
}

int munmap(void *buf, size_t len)
{
    return mvapich_munmap(buf, len);
}

#endif /* DISABLE_MUNMAP_HOOK */

#ifndef DISABLE_TRAP_SBRK

void *mvapich_sbrk(intptr_t delta)
{
    if (delta < 0) {

        void *current_brk = sbrk(0);

        mvapich_mem_unhook((void *)
                ((uintptr_t) current_brk + delta), -delta);

        /* -delta is actually a +ve number */
    }

    return sbrk(delta);
}

#endif /* DISABLE_TRAP_SBRK */


#endif /* DISABLE_PTMALLOC */

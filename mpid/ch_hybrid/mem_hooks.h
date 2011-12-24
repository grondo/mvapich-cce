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

#ifndef _MEM_HOOKS_H
#define _MEM_HOOKS_H

#ifndef DISABLE_PTMALLOC

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ptmalloc2/malloc.h"
#include "ptmalloc2/sysdeps/pthread/malloc-machine.h"

typedef struct {
    int         is_our_malloc;
    int         is_our_free;
    int         is_our_calloc;
    int         is_our_realloc;
    int         is_our_valloc;
    int         is_our_memalign;
    int         is_inside_free;
    int         is_mem_hook_finalized;
#ifndef DISABLE_MUNMAP_HOOK
    int         (*munmap)(void*, size_t);
#endif
} mvapich_malloc_info_t;

mvapich_malloc_info_t mvapich_minfo;

void mvapich_mem_unhook(void *mem, size_t size);
int  mvapich_minit(void);
void mvapich_mfin(void);

#ifndef DISABLE_MUNMAP_HOOK
int mvapich_munmap(void *buf, int len);
#endif

#ifndef DISABLE_TRAP_SBRK
void *mvapich_sbrk(intptr_t delta);
#endif

#endif /* DISABLE_PTMALLOC */

#endif /* _MEM_HOOKS_H */

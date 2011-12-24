/*
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Berkeley Lab as part of MVICH.
 *
 * Authors: Bill Saphir      <wcsaphir@lbl.gov>
 *          Michael Welcome  <mlwelcome@lbl.gov>
 */

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

/* Thanks to Voltaire for contributing enhancements to
 * registration cache implementation
 */

#include <stdlib.h>
#include <malloc.h>

#include "dreg.h"
#include "ibverbs_const.h"
#include "avl.h"

/*
 * dreg.c: everything having to do with dynamic memory registration. 
 */


/* statistics */
unsigned long dreg_stat_cache_hit, dreg_stat_cache_miss, dreg_stat_evicted;
static unsigned long pinned_pages_count;

struct dreg_entry *dreg_free_list;
struct dreg_entry *dreg_unused_list;
struct dreg_entry *dreg_unused_tail;

static int is_dreg_initialized = 0;


/*
 * Delete entry d from the double-linked unused list.
 * Take care if d is either the head or the tail of the list.
 */

#define DREG_REMOVE_FROM_UNUSED_LIST(d) {                           \
    dreg_entry *prev = (d)->prev_unused;                            \
        dreg_entry *next = (d)->next_unused;                        \
        (d)->next_unused = NULL;                                    \
        (d)->prev_unused = NULL;                                    \
        if (prev != NULL) {                                         \
            prev->next_unused = next;                               \
        }                                                           \
    if (next != NULL) {                                             \
        next->prev_unused = prev;                                   \
    }                                                               \
    if (dreg_unused_list == (d)) {                                  \
        dreg_unused_list = next;                                    \
    }                                                               \
    if (dreg_unused_tail == (d)) {                                  \
        dreg_unused_tail = prev;                                    \
    }                                                               \
}

/*
 * Add entries to the head of the unused list. dreg_evict() takes
 * them from the tail. This gives us a simple LRU mechanism 
 */

#define DREG_ADD_TO_UNUSED_LIST(d) {                                \
    d->next_unused = dreg_unused_list;                              \
    d->prev_unused = NULL;                                          \
    if (dreg_unused_list != NULL) {                                 \
        dreg_unused_list->prev_unused = d;                          \
    }                                                               \
    dreg_unused_list = d;                                           \
    if (NULL == dreg_unused_tail) {                                 \
        dreg_unused_tail = d;                                       \
    }                                                               \
}

#define DREG_GET_FROM_FREE_LIST(d) {                                \
    d = dreg_free_list;                                             \
    if (dreg_free_list != NULL) {                                   \
        dreg_free_list = dreg_free_list->next;                      \
    }                                                               \
}

#define DREG_ADD_TO_FREE_LIST(d) {                                  \
    d->next = dreg_free_list;                                       \
    dreg_free_list = d;                                             \
}


#define DREG_BEGIN(R) ((R)->pagenum)
#define DREG_END(R) ((R)->pagenum + (R)->npages - 1)

/* list element */
typedef struct _entry
{
	dreg_entry *reg;
	struct _entry *next;
} entry_t;

typedef struct _vma 
{
	unsigned long start;      /* first page number of the area */
	unsigned long end;        /* last page number of the area */
	entry_t *list;	  	  /* all dregs on this virtual memory region */
	unsigned long list_count;  /* number of elements on the list */
	struct _vma *next, *prev; /* double linked list of vmas */
} vma_t;

vma_t vma_list;
AVL_TREE *vma_tree;

#ifndef DISABLE_PTMALLOC
static pthread_spinlock_t dreg_lock = 0;
static pthread_spinlock_t dereg_lock = 0;
static pthread_t          th_id_of_lock = -1;
static pthread_t          th_id_of_dereg_lock = -1;

/* Array which stores the memory regions 
 * ptrs which are to be deregistered after 
 * free hook pulls them out of the reg cache
 */
static dreg_region *deregister_mr_array;

/* Number of pending deregistration
 * operations 
 * Note: This number can never exceed
 * the total number of reg. cache
 * entries
 */
static int n_dereg_mr = 0;

/* Keep free list of VMA data structs
 * and entries */
static vma_t vma_free_list;
static entry_t entry_free_list;

#define INIT_FREE_LIST(_list) {                                 \
    (_list)->next = NULL;                                       \
}

#define ADD_FREE_LIST(_list, _v) {                              \
    (_v)->next = (_list)->next;                                 \
    (_list)->next = (_v);                                       \
}

#define GET_FREE_LIST(_list, _v) {                              \
    *(_v) = (_list)->next;                                      \
    if((_list)->next) {                                         \
        (_list)->next = (_list)->next->next;                    \
    }                                                           \
}

#endif

/* Tree functions */
static long vma_compare (void *a, void *b)
{
	const vma_t *vma1 = *(vma_t**)a, *vma2 = *(vma_t**)b;
	return vma1->start - vma2->start;
}

static long vma_compare_search (void *a, void *b)
{
	const vma_t *vma = *(vma_t**)b;
	const unsigned long addr = (unsigned long)a;

	if (vma->end < addr)
		return 1;

	if (vma->start <= addr)
		return 0;

	return -1;
}

static long vma_compare_closest (void *a, void *b)
{
	const vma_t *vma = *(vma_t**)b;
	const unsigned long addr = (unsigned long)a;

	if (vma->end < addr)
		return 1;

	if (vma->start <= addr)
		return 0;

	if (vma->prev->end < addr)
		return 0;

	return -1;
}

static inline vma_t *vma_search (unsigned long addr)
{
	vma_t **vma;
	vma = avlfindex (vma_compare_search, (void*)addr, vma_tree);
	return vma?(*vma):NULL;
}


static unsigned long  avldatasize (void)
{
	return (unsigned long) (sizeof (void *));
}

static inline vma_t *vma_new (unsigned long start, unsigned long end)
{
	vma_t *vma;

	if (viadev_dreg_cache_limit &&
			pinned_pages_count + (end - start + 1) > 
				viadev_dreg_cache_limit)
		return NULL;

#ifndef DISABLE_PTMALLOC
    GET_FREE_LIST(&vma_free_list, &vma);

    if(NULL == vma) {
        vma = malloc(sizeof (vma_t));
    }
#else
    vma = malloc (sizeof (vma_t));
#endif 

	if (vma == NULL)
		return NULL;

	vma->start = start;
	vma->end = end;
	vma->next = vma->prev = NULL;
	vma->list = NULL;
	vma->list_count = 0;

	avlins (&vma, vma_tree);

	pinned_pages_count += (vma->end - vma->start + 1);

	return vma;
}

static inline void vma_remove (vma_t *vma)
{
	avldel (&vma, vma_tree);
	pinned_pages_count -= (vma->end - vma->start + 1);
}

static inline void vma_destroy (vma_t *vma)
{
	entry_t *e = vma->list;

	while (e)
	{
		entry_t *t = e;
		e = e->next;
#ifndef DISABLE_PTMALLOC
        ADD_FREE_LIST(&entry_free_list, t);
#else
        free (t);
#endif
	}

#ifndef DISABLE_PTMALLOC
    ADD_FREE_LIST(&vma_free_list, vma);
#else
    free (vma);
#endif
}

static inline long compare_dregs (entry_t *e1, entry_t *e2)
{
	if (DREG_END (e1->reg) != DREG_END (e2->reg))
		return DREG_END (e1->reg) - DREG_END (e2->reg);

	/* tie breaker */
    return 0;
}

/* add entry to list of dregs. List sorted by region last page number */
static inline void add_entry (vma_t *vma, dreg_entry *r)
{
	entry_t **i, *e;

#ifndef DISABLE_PTMALLOC
    GET_FREE_LIST(&entry_free_list, &e);

    if(NULL == e) {
        e = malloc(sizeof(entry_t));
    }
#else
    e = malloc (sizeof (entry_t));
#endif

	if (e == NULL)
		return;

	e->reg = r;
	
	for (i = &vma->list; *i != NULL && compare_dregs (*i, e) > 0; i=&(*i)->next);

	e->next = (*i);
	(*i) = e;
	vma->list_count++;
}

static inline void remove_entry (vma_t *vma, dreg_entry *r)
{
	entry_t **i;

	for (i = &vma->list; *i != NULL && (*i)->reg != r; i=&(*i)->next);

	if (*i)
	{
		entry_t *e = *i;
		*i = (*i)->next;
#ifndef DISABLE_PTMALLOC
        ADD_FREE_LIST(&entry_free_list, e);
#else
        free (e);
#endif
		vma->list_count--;
	}
}

static inline void copy_list (vma_t *to, vma_t *from)
{
	entry_t *f = from->list, **t = &to->list;

	while (f)
	{
		entry_t *e;

#ifndef DISABLE_PTMALLOC
        GET_FREE_LIST(&entry_free_list, &e);

        if(NULL == e) {
            e = malloc(sizeof(entry_t));
        }
#else
        e = malloc (sizeof (entry_t));
#endif

		e->reg = f->reg;
		e->next = NULL;

		*t = e;
		t = &e->next;
		f = f->next;
	}
	to->list_count = from->list_count;
}

/* returns 1 iff two lists contain the same entries */
static inline int compare_lists (vma_t *vma1, vma_t *vma2)
{
	entry_t *e1 = vma1->list, *e2 = vma2->list;

	if (vma1->list_count != vma2->list_count)
		return 0;

	while (1)
	{
		if (e1 == NULL || e2 == NULL)
			return 1;

		if (e1->reg != e2->reg)
			break;

		e1 = e1->next;
		e2 = e2->next;
	}

	return 0;
}

static int dreg_remove (dreg_entry *r)
{
	vma_t  *vma;


	vma = vma_search (DREG_BEGIN (r));


	if (vma == NULL)  /* no such region in database */
		return -1;

	while (vma != &vma_list && vma->start <= DREG_END (r))
	{
		remove_entry (vma, r);


		if (vma->list == NULL)
		{
			vma_t *next = vma->next;

			vma_remove (vma);
			vma->prev->next = vma->next;
			vma->next->prev = vma->prev;
			vma_destroy (vma);
			vma = next;
		}
		else
		{
			int merged;

			do {
				merged = 0;
				if (vma->start == vma->prev->end + 1 &&
						compare_lists (vma, vma->prev))
				{
					vma_t *t = vma;
					vma = vma->prev;
					vma->end = t->end;
					vma->next = t->next;
					vma->next->prev = vma;
					vma_remove (t);
					vma_destroy (t);
					merged = 1;
				}
				if (vma->end + 1 == vma->next->start &&
						compare_lists (vma, vma->next))
				{
					vma_t *t = vma->next;
					vma->end = t->end;
					vma->next = t->next;
					vma->next->prev = vma;
					vma_remove (t);
					vma_destroy (t);
					merged = 1;
				}
			} while (merged);
			vma = vma->next;
		}
	}

	return 0;
}

static int dreg_insert (dreg_entry *r)
{
	vma_t *i = &vma_list, **v;
	unsigned long begin = DREG_BEGIN (r), end = DREG_END (r);

	v = avlfindex (vma_compare_closest, (void*)begin, vma_tree);

	if (v)
		i = *v;

	while (begin <= end)
	{
		vma_t *vma;

		if (i == &vma_list)
		{
			vma = vma_new (begin, end);

			if (!vma)
				goto remove;

			vma->next = i;
			vma->prev = i->prev;
			i->prev->next = vma;
			i->prev = vma;

			begin = vma->end + 1;

			add_entry (vma, r);
		} 
		else if (i->start > begin)
		{
			vma = vma_new (begin, 
					(i->start <= end)?(i->start - 1):end);

			if (!vma)
				goto remove;

			/* insert before */
			vma->next = i;
			vma->prev = i->prev;
			i->prev->next = vma;
			i->prev = vma;

			i = vma;

			begin = vma->end + 1;

			add_entry (vma, r);
		}
		else if (i->start == begin)
		{
			if (i->end > end)
			{
				vma = vma_new (end+1, i->end);
				
				if (!vma)
					goto remove;

				i->end = end;

				copy_list (vma, i);

				/* add after */
				vma->next = i->next;
				vma->prev = i;
				i->next->prev = vma;
				i->next = vma;

				add_entry (i, r);
				begin = end + 1;
			}
			else
			{
				add_entry (i, r);
				begin = i->end + 1;
			}
		}
		else
		{
			vma = vma_new (begin, i->end);

			if (!vma)
				goto remove;

			i->end = begin - 1;

			copy_list (vma, i);

			/* add after */
			vma->next = i->next;
			vma->prev = i;
			i->next->prev = vma;
			i->next = vma;
		}

		i = i->next;
	}
	return 0;

remove:
	dreg_remove (r);
	return -1;
}


static inline dreg_entry *dreg_lookup (unsigned long begin, unsigned long end )
{
	vma_t *vma;

	vma = vma_search (begin);

	if (!vma)
		return NULL;

    if(!vma->list)
        return NULL;

	if (DREG_END (vma->list->reg) >= end)
		return vma->list->reg;

	return NULL;
}

void vma_db_init (void)
{
	vma_tree = avlinit (vma_compare, avldatasize);
	vma_list.next = &vma_list;
	vma_list.prev = &vma_list;
	vma_list.list = NULL; 
	vma_list.list_count = 0;
}

void dreg_init()
{
    int i;

    pinned_pages_count = 0;
    vma_db_init ();
    dreg_free_list = (dreg_entry *)
        malloc(sizeof(dreg_entry) * viadev_ndreg_entries);

    if (dreg_free_list == NULL) {
	   error_abort_all(GEN_EXIT_ERR,
                            "dreg_init: unable to malloc %d bytes",
                            (int) sizeof(dreg_entry) * viadev_ndreg_entries);
    }

    for (i = 0; i < (int) viadev_ndreg_entries - 1; i++) {
        dreg_free_list[i].next = &dreg_free_list[i + 1];
    }

    dreg_free_list[viadev_ndreg_entries - 1].next = NULL;

    dreg_unused_list = NULL;
    dreg_unused_tail = NULL;
    /* cache hit and miss time stat variables initialization */

    is_dreg_initialized = 1;

#ifndef DISABLE_PTMALLOC
    pthread_spin_init(&dreg_lock, 0);
    pthread_spin_init(&dereg_lock, 0);
    deregister_mr_array = (dreg_region *)
        malloc(sizeof(dreg_region) * viadev_ndreg_entries);

    if(NULL == deregister_mr_array) {
        error_abort_all(GEN_EXIT_ERR,
                "dreg_init: unable to malloc %d bytes",
                (int) sizeof(struct ibv_mr*) *
                viadev_ndreg_entries);
    }

    memset(deregister_mr_array, 0,
            sizeof(dreg_region) * viadev_ndreg_entries);

    INIT_FREE_LIST(&vma_free_list);

    INIT_FREE_LIST(&entry_free_list);

#endif
}

#ifndef DISABLE_PTMALLOC

static int have_dereg() {
    return pthread_equal(th_id_of_dereg_lock, pthread_self());
}

static void lock_dereg()
{
    pthread_spin_lock(&dereg_lock);
    th_id_of_dereg_lock = pthread_self();
}

static void unlock_dereg()
{
    th_id_of_dereg_lock = -1;
    pthread_spin_unlock(&dereg_lock);
}

static int have_dreg() {
    return pthread_equal(th_id_of_lock, pthread_self());
}

static void lock_dreg()
{
    pthread_spin_lock(&dreg_lock);
    th_id_of_lock = pthread_self();
}

static void unlock_dreg()
{
    th_id_of_lock = -1;
    pthread_spin_unlock(&dreg_lock);
}

/* 
 * Check if we have to deregister some memory regions
 * which were previously marked invalid by free hook 
 */

void flush_dereg_mrs_external()
{
    unsigned long i, j;
    unsigned long pagenum_low, pagenum_high;
    unsigned long  npages, begin, end;
    unsigned long user_low_a, user_high_a;
    unsigned long pagebase_low_a, pagebase_high_a;
    struct dreg_entry *d;
    void *addr;

    if(n_dereg_mr == 0 || have_dreg() || have_dereg()) {
        return;
    }

    lock_dreg();
    lock_dereg();

    for(j = 0; j < n_dereg_mr; j++) {
        void *buf; 
        size_t len;

        buf = deregister_mr_array[j].buf;
        len = deregister_mr_array[j].len;

        /* calculate base page address for registration */
        user_low_a = (unsigned long) buf;
        user_high_a = user_low_a + (unsigned long) len - 1;

        pagebase_low_a = user_low_a & ~DREG_PAGEMASK;
        pagebase_high_a = user_high_a & ~DREG_PAGEMASK;

        /* info to store in hash table */
        pagenum_low = pagebase_low_a >> DREG_PAGEBITS;
        pagenum_high = pagebase_high_a >> DREG_PAGEBITS;
        npages = 1 + (pagenum_high - pagenum_low);

        /* For every page in this buffer find out whether
         * it is registered or not. This is fine, since
         * we register only at a page granularity */

        for(i = 0; i < npages; i++) {
            addr = (void *) ((uintptr_t) pagebase_low_a + i * DREG_PAGESIZE);

            begin = ((unsigned long)addr) >> DREG_PAGEBITS;

            end = ((unsigned long)(((char*)addr) + 
                        DREG_PAGESIZE - 1)) >> DREG_PAGEBITS;

            while( (d = dreg_lookup (begin, end)) != NULL) { 

                if((d->refcount != 0) || (d->is_valid == 0)) {
                    /* This memory area is still being referenced
                     * by other pending MPI operations, which are
                     * expected to call dreg_unregister and thus
                     * unpin the buffer. We cannot deregister this
                     * page, since other ops are pending from here. */

                    /* OR: This memory region is in the process of
                     * being deregistered. Leave it alone! */
                    continue;
                }       

                d->is_valid = 0;

                if(d->memhandle) {
                    if(ibv_dereg_mr(d->memhandle)) {
                        error_abort_all(IBV_RETURN_ERR,
                            "deregistration failed\n");
                    }
                }       

                d->memhandle = NULL; 

                if(d->refcount == 0) { 
                    if(viadev_use_dreg_cache) {
                        DREG_REMOVE_FROM_UNUSED_LIST(d);
                    }
                } else {
                    d->refcount--;
                }

                dreg_remove (d);
                DREG_ADD_TO_FREE_LIST(d);
            }       
        }

    }

    n_dereg_mr = 0;
    unlock_dereg();
    unlock_dreg();
}
#endif


/* will return a NULL pointer if registration fails */
dreg_entry *dreg_register(void *buf, int len, int acl)
{
    struct dreg_entry *d;
    int rc;

#ifndef DISABLE_PTMALLOC
    lock_dreg();
#endif

    d = dreg_find(buf, len, acl);

    if (d != NULL) {
        dreg_stat_cache_hit++;
        dreg_incr_refcount(d);
	
    } else {
	    dreg_stat_cache_miss++;
           while ((d = dreg_new_entry(buf, len, acl)) == NULL) {
            /* either was not able to obtain a dreg_entry data strucrure
             * or was not able to register memory.  In either case,
             * attempt to evict a currently unused entry and try again.
             */
            rc = dreg_evict();
            if (rc == 0) {
                /* could not evict anything, will not be able to
                 * register this memory.  Return failure.
                 */
#ifndef DISABLE_PTMALLOC
                unlock_dreg();
#endif
                return NULL;
            }
            /* eviction successful, try again */
        }

        dreg_incr_refcount(d);

    }

#ifndef DISABLE_PTMALLOC
    unlock_dreg();
#endif

    return d;
}

void dreg_unregister(dreg_entry * d)
{
#ifndef DISABLE_PTMALLOC
    lock_dreg();
#endif
    dreg_decr_refcount(d);
#ifndef DISABLE_PTMALLOC
    unlock_dreg();
#endif
}

/* If acl == -1, then acl check is not made */
dreg_entry *dreg_find(void *buf, int len, int acl)
{
    dreg_entry *d;

    unsigned long begin = ((unsigned long)buf) >> DREG_PAGEBITS;
    unsigned long end = ((unsigned long)(((char*)buf) + len - 1)) >> DREG_PAGEBITS;

    d = dreg_lookup(begin, end);

    /* If there is no acl check requested,
     * just return the entry as is */
    if(DREG_NO_ACL_CHECK == acl) {
        return d;
    }

    /* ACL check is requested, so see
     * if we can match the ACL */
    if(NULL != d) {
        if(d->acl <= acl) {
            return d;
        } else {
            return NULL;
        }
    } else {
        return d;
    }
}


/*
 * get a fresh entry off the free list. 
 * Ok to return NULL. Higher levels will deal with it. 
 */

dreg_entry *dreg_get()
{
    dreg_entry *d;
    DREG_GET_FROM_FREE_LIST(d);

    if (d != NULL) {
        d->refcount = 0;
        d->next_unused = NULL;
        d->prev_unused = NULL;
        d->next = NULL;
    } else {
        T_PRINT("dreg_get: no free dreg entries");
    }
    return (d);
}

void dreg_release(dreg_entry * d)
{
    /* note this correctly handles appending to empty free list */
    d->next = dreg_free_list;
    dreg_free_list = d;
}

/*
 * Decrement reference count on a dreg entry. If ref count goes to 
 * zero, don't free it, but put it on the unused list so we
 * can evict it if necessary. Put on head of unused list. 
 */
void dreg_decr_refcount(dreg_entry * d)
{
    void *buf;
    aint_t bufint;

    assert(d->refcount > 0);
    d->refcount--;
    if (d->refcount == 0) {
        if(viadev_use_dreg_cache) {
            DREG_ADD_TO_UNUSED_LIST(d);
        } else {
            bufint = d->pagenum << DREG_PAGEBITS;
            buf = (void *) bufint;

            d->is_valid = 0;
            if (deregister_memory(d->memhandle)) {
                error_abort_all(IBV_RETURN_ERR, "deregister fails\n");
            }
            d->memhandle = NULL;
            dreg_remove (d);
            DREG_ADD_TO_FREE_LIST(d);
        }
    }
    T_PRINT("decr_refcount: entry " AINT_FORMAT
            " refcount" AINT_FORMAT " memhandle" AINT_FORMAT,
            (aint_t) d, (aint_t) d->refcount, (aint_t) d->memhandle);
}

/*
 * Increment reference count on a dreg entry. If reference count
 * was zero and it was on the unused list (meaning it had been
 * previously used, and the refcount had been decremented),
 * we should take it off
 */

void dreg_incr_refcount(dreg_entry * d)
{
    assert(d != NULL);
    if (d->refcount == 0) {
        DREG_REMOVE_FROM_UNUSED_LIST(d);
    }
    d->refcount++;
}

/*
 * Evict a registration. This means delete it from the unused list, 
 * add it to the free list, and deregister the associated memory.
 * Return 1 if success, 0 if nothing to evict.
 */
int dreg_evict()
{
    void *buf;
    dreg_entry *d;
    aint_t bufint;

    d = dreg_unused_tail;
    if (d == NULL) {
        /* no entries left on unused list, return failure */
        return 0;
    }
    
    DREG_REMOVE_FROM_UNUSED_LIST(d);

    assert(d->refcount == 0);
    bufint = d->pagenum << DREG_PAGEBITS;
    buf = (void *) bufint;

    d->is_valid = 0;
    if (deregister_memory(d->memhandle)) {
        error_abort_all(IBV_RETURN_ERR,
                        "Deregister fails\n");
    }

    dreg_remove (d);

    DREG_ADD_TO_FREE_LIST(d);

    dreg_stat_evicted++;
    return 1;
}



/*
 * dreg_new_entry is called only when we have already
 * found that the memory isn't registered. Register it 
 * and put it in the hash table 
 */

dreg_entry *dreg_new_entry(void *buf, int len, int acl)
{


    dreg_entry *d;
    aint_t pagenum_low, pagenum_high;
    unsigned long  npages;
    /* user_low_a is the bottom address the user wants to register;
     * user_high_a is one greater than the top address the 
     * user wants to register
     */
    aint_t user_low_a, user_high_a;
    /* pagebase_low_a and pagebase_high_a are the addresses of 
     * the beginning of the page containing user_low_a and 
     * user_high_a respectively. 
     */

    aint_t pagebase_low_a, pagebase_high_a;
    void *pagebase_low_p;
    unsigned long register_nbytes;

    d = dreg_get();
    if (NULL == d) {
        return d;
    }

    /* calculate base page address for registration */
    user_low_a = (aint_t) buf;
    user_high_a = user_low_a + (aint_t) len - 1;

    pagebase_low_a = user_low_a & ~DREG_PAGEMASK;
    pagebase_high_a = user_high_a & ~DREG_PAGEMASK;

    /* info to store in hash table */
    pagenum_low = pagebase_low_a >> DREG_PAGEBITS;
    pagenum_high = pagebase_high_a >> DREG_PAGEBITS;
    npages = 1 + (pagenum_high - pagenum_low);

    if ( viadev_dreg_cache_limit != 0 && 
		  npages >= (int) viadev_dreg_cache_limit ) {
        return NULL;
    }

    pagebase_low_p = (void *) pagebase_low_a;
    register_nbytes = npages * DREG_PAGESIZE;

    d->pagenum = pagenum_low;
    d->npages = npages;
    d->acl = acl;

    if (dreg_insert (d) < 0)
    {
	    dreg_release(d);
	    return NULL;
    }

    if(acl) {
        d->memhandle = ibv_reg_mr(viadev.ptag, (void *) pagebase_low_p, 
                register_nbytes, IBV_ACCESS_REMOTE_READ);
    } else {
        d->memhandle = ibv_reg_mr(viadev.ptag, (void *) pagebase_low_p, 
                register_nbytes, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                IBV_ACCESS_REMOTE_READ);
    }

    /* if not success, return NULL to indicate that we were unable to
     * register this memory.  */
    if (!d->memhandle) {
	dreg_remove (d);
        dreg_release(d);
        return NULL;
    }

    d->is_valid = 1;

    return d;
}

#ifndef DISABLE_PTMALLOC

void find_and_free_dregs_inside(void *buf, size_t len)
{
    if(!is_dreg_initialized ||
            !viadev_use_dreg_cache) {
        return; 
    }

    if(have_dereg() || have_dreg()) {
        return;
    }

    lock_dereg();
    deregister_mr_array[n_dereg_mr].buf = buf;
    deregister_mr_array[n_dereg_mr].len = len;

    n_dereg_mr++;
    unlock_dereg();
}

#endif   /* !DISABLE_PTMALLOC */

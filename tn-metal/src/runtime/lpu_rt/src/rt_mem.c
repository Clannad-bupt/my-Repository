/* rt_mem.c — device DRAM pool (first-fit + free list over the BAR1 window, metadata on the host
 * side) */
#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

lpuRtError rt_pool_init(lpu_pool_t *p, uint8_t *win, uint64_t dev_base, uint64_t size) {
    if (!win || size == 0)
        return LPU_RT_ERROR_INVALID_VALUE;

    memset(p, 0, sizeof(*p));
    p->win = win;
    p->dev_base = dev_base;
    p->size = size;
    pthread_mutex_init(&p->lock, NULL);

    /* one free block spanning the whole window */
    lpu_memblk_t *b = calloc(1, sizeof(*b));
    if (!b)
        return LPU_RT_ERROR_OUT_OF_MEMORY;
    b->off = 0;
    b->size = size;
    b->bsize = size;
    b->free = true;
    p->head = b;
    return LPU_RT_SUCCESS;
}

void rt_pool_fini(lpu_pool_t *p) {
    lpu_memblk_t *b = p->head;
    while (b) {
        lpu_memblk_t *n = b->next;
        free(b);
        b = n;
    }
    pthread_mutex_destroy(&p->lock);
    memset(p, 0, sizeof(*p));
}

static uint64_t round_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

lpuRtError rt_pool_alloc(lpu_pool_t *p, uint64_t size, uint64_t align, lpu_memblk_t **blk,
                         uint64_t *dev_addr) {
    if (size == 0 || (align & (align - 1)) != 0)
        return LPU_RT_ERROR_INVALID_VALUE;

    uint64_t want = round_up(size, LPU_POOL_ALIGN);

    pthread_mutex_lock(&p->lock);
    for (lpu_memblk_t *b = p->head; b; b = b->next) {
        if (!b->free)
            continue;
        /* alignment basis = device-side address (descriptor pointers align by device address) */
        uint64_t devaddr = p->dev_base + b->off;
        uint64_t aligned = round_up(devaddr, align);
        uint64_t off_in_blk = aligned - devaddr;
        if (b->bsize < off_in_blk + want)
            continue;

        if (off_in_blk > 0) {
            /* split an alignment stub off the front (keeps the list address-ordered) */
            lpu_memblk_t *pre = calloc(1, sizeof(*pre));
            if (!pre)
                break;
            pre->off = b->off;
            pre->bsize = off_in_blk;
            pre->size = off_in_blk;
            pre->free = true;
            lpu_memblk_t **pp = &p->head;
            while (*pp != b)
                pp = &(*pp)->next;
            *pp = pre;
            pre->next = b;
            b->off += off_in_blk;
            b->bsize -= off_in_blk;
        }
        if (b->bsize > want) {
            /* split the remainder off the back as a free block */
            lpu_memblk_t *post = calloc(1, sizeof(*post));
            if (!post)
                break;
            post->off = b->off + want;
            post->bsize = b->bsize - want;
            post->size = post->bsize;
            post->free = true;
            post->next = b->next;
            b->next = post;
        }
        b->size = size;
        b->bsize = want;
        b->free = false;
        pthread_mutex_unlock(&p->lock);
        *blk = b;
        *dev_addr = p->dev_base + b->off;
        return LPU_RT_SUCCESS;
    }
    pthread_mutex_unlock(&p->lock);
    return LPU_RT_ERROR_OUT_OF_MEMORY;
}

void rt_pool_free(lpu_pool_t *p, lpu_memblk_t *blk) {
    if (!blk)
        return;
    pthread_mutex_lock(&p->lock);
    blk->free = true;
    /* merge adjacent free blocks (list is address-ordered) */
    lpu_memblk_t *b = p->head;
    while (b) {
        if (b->free && b->next && b->next->free && b->off + b->bsize == b->next->off) {
            lpu_memblk_t *n = b->next;
            b->bsize += n->bsize;
            b->size = b->bsize;
            b->next = n->next;
            free(n);
            continue;
        }
        b = b->next;
    }
    pthread_mutex_unlock(&p->lock);
}

/* ---------------- public API ---------------- */

LPU_RT_API lpuRtError lpuRtMalloc(lpuRtDevicePtr *devPtr, size_t size) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!devPtr || size == 0)
        return LPU_RT_ERROR_INVALID_VALUE;

    lpu_memblk_t *blk = NULL;
    uint64_t addr = 0;
    lpuRtError e = rt_pool_alloc(&c->pool, size, 256, &blk, &addr);
    if (e != LPU_RT_SUCCESS) {
        rt_set_last_error(e);
        return e;
    }

    /* Where is the host-side metadata of the allocation kept? — free needs to
       find the blk back: (off → blk) via a walk of the pool list (offsets are unique) */
    blk->size = size; /* record the original request size */
    *devPtr = addr;
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtFree(lpuRtDevicePtr devPtr) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (devPtr < c->pool.dev_base || devPtr >= c->pool.dev_base + c->pool.size)
        return LPU_RT_ERROR_INVALID_VALUE;

    uint64_t off = devPtr - c->pool.dev_base;
    pthread_mutex_lock(&c->pool.lock);
    lpu_memblk_t *found = NULL;
    for (lpu_memblk_t *b = c->pool.head; b; b = b->next) {
        if (b->off == off && !b->free) {
            found = b;
            break;
        }
    }
    pthread_mutex_unlock(&c->pool.lock);
    if (!found)
        return LPU_RT_ERROR_INVALID_VALUE;

    rt_pool_free(&c->pool, found);
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtMemGetInfo(uint64_t *freeBytes, uint64_t *totalBytes) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!freeBytes || !totalBytes)
        return LPU_RT_ERROR_INVALID_VALUE;

    uint64_t free_bytes = 0;
    pthread_mutex_lock(&c->pool.lock);
    for (lpu_memblk_t *b = c->pool.head; b; b = b->next)
        if (b->free)
            free_bytes += b->bsize;
    pthread_mutex_unlock(&c->pool.lock);

    *freeBytes = free_bytes;
    *totalBytes = c->pool.size;
    return LPU_RT_SUCCESS;
}

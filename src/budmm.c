#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "budmm.h"
#include "budhelper.h"
#include "budprint.h"

extern bud_free_block free_list_heads[NUM_FREE_LIST];
void *bud_realloc(void *ptr, uint32_t rsize)
{
    if (ptr == NULL)
        return bud_malloc(rsize);

    if (!rsize)
    {
        bud_free(ptr);
        return NULL;
    }
    if (rsize > (MAX_BLOCK_SIZE - sizeof(bud_header)))
    {
        errno = EINVAL;
        return NULL;
    }
    bud_header *b_header = (bud_header *)(((uintptr_t)ptr) - sizeof(bud_header));
    if (invalid_pointer(b_header))
        abort();

    uint32_t r_order = get_order(rsize);
    if (r_order == b_header->order)
    {
        b_header->rsize = rsize;
        b_header->padded = is_padded(b_header);
        return ptr;
    }
    if (r_order > b_header->order)
    {
        void *new_block = bud_malloc(rsize);
        if (new_block == NULL)
        {
            errno = ENOMEM;
            return NULL;
        }
        memcpy(new_block, ptr, ORDER_TO_BLOCK_SIZE(b_header->order) - sizeof(bud_header));
        bud_free(ptr);
        return new_block;
    }
    if (r_order < b_header->order)
    {
        split((bud_free_block *)b_header, b_header->order, r_order);
        b_header->allocated = 1;
        b_header->order = r_order;
        b_header->rsize = rsize;
        b_header->padded = is_padded(b_header);
        return ptr;
    }
    return NULL;
}

void bud_free(void *bp)
{
    if (bp == NULL)
        return;

    bud_header *b_header = (bud_header *)(((uintptr_t)bp) - sizeof(bud_header));
    if (invalid_pointer(b_header))
        abort();

    bud_free_block *current_block = (bud_free_block *)b_header;
    insert_free_block(current_block->header.order - ORDER_MIN, current_block);
    coalesce(current_block);
}

int valid_buddy_block(bud_free_block *current_block, bud_free_block *buddy_block, int current_block_size)
{
    return is_in_heap((uintptr_t)buddy_block) &&
           (((uintptr_t)current_block) ^ current_block_size) == ((uintptr_t)buddy_block) &&
           (buddy_block->header.order == current_block->header.order);
}

int is_in_heap(uintptr_t buddy_block)
{
    return (buddy_block < (uintptr_t)bud_heap_end() &&
            buddy_block >= (uintptr_t)bud_heap_start());
}

void merge_blocks(bud_free_block *left_block, bud_free_block *right_block, int left_merge)
{
    remove_free_block(left_block->header.order - ORDER_MIN, left_block);
    remove_free_block(right_block->header.order - ORDER_MIN, right_block);
    if (left_merge)
        left_block = right_block;
    left_block->header.order += 1;
    insert_free_block(left_block->header.order - ORDER_MIN, left_block);
}

void coalesce(bud_free_block *current_block)
{
    while (1)
    {
        int current_block_size = ORDER_TO_BLOCK_SIZE(current_block->header.order);

        // check right block
        bud_free_block *buddy_block =
            (bud_free_block *)(((uintptr_t)current_block) + current_block_size);
        if (valid_buddy_block(current_block, buddy_block, current_block_size))
        {
            if (!buddy_block->header.allocated)
            {
                merge_blocks(current_block, buddy_block, 0);
                continue;
            }
            else
            {
                // buddy block currently allocated
                return;
            }
        }
        // check left block
        buddy_block =
            (bud_free_block *)(((uintptr_t)current_block) - current_block_size);
        if (valid_buddy_block(current_block, buddy_block, current_block_size))
        {
            if (!buddy_block->header.allocated)
            {
                merge_blocks(current_block, buddy_block, 1);
                current_block = buddy_block;
                continue;
            }
            else
            {
                // buddy block currently allocated
                return;
            }
        }
        // if neither right nor left are valid buddy blocks
        return;
    }
}

int invalid_pointer(bud_header *b_header)
{
    // if pointer outside of bud_heap
    if ((uintptr_t)bud_heap_start() > (uintptr_t)b_header ||
        (uintptr_t)b_header >= (uintptr_t)bud_heap_end())
    {
        return 1;
    }
    if ((((uintptr_t)b_header) % sizeof(bud_header)) != 0)
    {
        return 1;
    }
    if ((b_header->order > ORDER_MAX) ||
        (b_header->order < ORDER_MIN))
    {
        return 1;
    }
    if (b_header->allocated == 0)
    {
        return 1;
    }
    if (b_header->padded == 0 && is_padded(b_header))
    {
        return 1;
    }
    if (b_header->padded == 1 && !is_padded(b_header))
    {
        return 1;
    }
    if (get_order(b_header->rsize) != b_header->order)
    {
        return 1;
    }
    return 0;
}

void *bud_malloc(uint32_t rsize)
{
    if (rsize <= 0 || rsize > (MAX_BLOCK_SIZE - sizeof(bud_header)))
    {
        errno = EINVAL;
        return NULL;
    }
    void *bp;
    uint32_t r_order = get_order(rsize);

    if ((bp = find_fit(r_order)) == NULL)
    {
        // no fit found, ask for more space
        if ((bp = bud_sbrk()) == (void *)-1)
        {
            errno = ENOMEM;
            return NULL;
        }
        // insert new block into max list
        bud_free_block *max_block = (bud_free_block *)bp;
        max_block->header.order = ORDER_MAX - 1;
        insert_free_block(NUM_FREE_LIST - 1, max_block);
    }
    place(bp, r_order);
    bud_header *b_header = (bud_header *)bp;
    b_header->rsize = rsize;
    b_header->padded = is_padded(b_header);
    return (void *)((uintptr_t)bp) + sizeof(bud_header);
}

void *find_fit(uint32_t r_order)
{
    for (int i = r_order - ORDER_MIN; i < NUM_FREE_LIST; ++i)
    {
        // if free list nonempty
        if (free_list_heads[i].next != &free_list_heads[i])
            return free_list_heads[i].next;
    }
    return NULL;
}

void split(bud_free_block *bp, int b_order, int r_order)
{
    for (int i = b_order - ORDER_MIN; i > (r_order - ORDER_MIN); --i)
    {
        int block_size = ORDER_TO_BLOCK_SIZE(i + ORDER_MIN);
        bud_free_block *right_block = (bud_free_block *)(((uintptr_t)bp) + (block_size / 2));
        right_block->header.order = i + ORDER_MIN - 1;
        insert_free_block(i - 1, right_block);
    }
}

void place(bud_free_block *bp, uint32_t r_order)
{
    int bp_order = bp->header.order;

    // remove block from free list
    remove_free_block(bp_order - ORDER_MIN, bp);

    // split if necessary
    split(bp, bp_order, r_order);

    bp->header.allocated = 1;
    bp->header.order = r_order;
}

uint32_t round_up_size_class(uint32_t size)
{
    uint32_t min_block_size = ORDER_TO_BLOCK_SIZE(ORDER_MIN);

    // if already power of 2 and at least min block size
    if (size && !(size & (size - 1)) && size >= min_block_size)
        return size;
    else if (size <= min_block_size)
        return min_block_size;

    uint32_t count = 0;
    while (size != 0)
    {
        size >>= 1;
        ++count;
    }
    return (1 << count) >= min_block_size ? 1 << count : min_block_size;
}

int get_order(uint32_t size)
{
    size = round_up_size_class(size + sizeof(bud_header));
    int order = 0;
    while (size >>= 1)
        ++order;

    return order;
}

void remove_free_block(int i, bud_free_block *bp)
{
    free_list_heads[i].next = bp->next;
    free_list_heads[i].next->prev = &free_list_heads[i];

    // if only element in list, set sentinel prev to itself
    if (free_list_heads[i].prev == bp)
        free_list_heads[i].prev = &free_list_heads[i];
}

void insert_free_block(int i, bud_free_block *bp)
{
    bp->header.allocated = 0;
    free_list_heads[i].next->prev = bp;
    bp->next = free_list_heads[i].next;
    bp->prev = &free_list_heads[i];
    free_list_heads[i].next = bp;

    // if list empty except for sentinel
    if (free_list_heads[i].prev == &free_list_heads[i])
        free_list_heads[i].prev = bp;
}

int is_padded(bud_header *b_header)
{
    return (b_header->rsize + sizeof(bud_header)) !=
           (ORDER_TO_BLOCK_SIZE(b_header->order));
}
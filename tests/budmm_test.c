#include <criterion/criterion.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <regex.h>
#include "budmm.h"
#define UNALLOCATED 0
#define UNPADDED 0
#define ALLOCATED 1
#define PADDED 1

#define HEADER_TO_PAYLOAD(hdr) (((char *)hdr) + sizeof(bud_header))
#define PAYLOAD_TO_HEADER(ptr) (bud_header *)(((char *)ptr - sizeof(bud_header)))

// provided helpers
static int free_list_is_empty(int order)
{
    int i = order - ORDER_MIN;
    return (free_list_heads[i].next == &free_list_heads[i]);
}

static void assert_empty_free_list(int order)
{
    cr_assert_neq(free_list_is_empty(order), 0,
                  "List [%d] contains an unexpected block!", order - ORDER_MIN);
}

static void assert_nonempty_free_list(int order)
{
    cr_assert_eq(free_list_is_empty(order), 0,
                 "List [%d] should not be empty!", order - ORDER_MIN);
}

void assert_null_free_lists()
{
    for (int order = ORDER_MIN; order < ORDER_MAX; order++)
        assert_empty_free_list(order);
}

void expect_errno_value(int exp_errno)
{
    cr_assert(errno == exp_errno,
              "`errno` was incorrectly set. Expected [%d] Actual: [%d]\n",
              exp_errno, errno);
}

void assert_header_values(bud_header *bhdr, int exp_alloc, int exp_order,
                          int exp_pad, int exp_req_sz)
{
    cr_assert(bhdr->allocated == exp_alloc,
              "header `allocated` bits were not properly set. Expected: [%d] Actual: [%d]\n",
              exp_alloc, bhdr->allocated);
    cr_assert(bhdr->order == exp_order,
              "header `order` bits were not properly set. Expected: [%d] Actual: [%d]\n",
              exp_order, bhdr->order);
    cr_assert(bhdr->padded == exp_pad,
              "header `padded` bits were not properly set. Expected: [%d] Actual: [%d]\n",
              exp_pad, bhdr->padded);
    cr_assert(bhdr->rsize == exp_req_sz,
              "header `rsize` bits were not properly set. Expected: [%d] Actual: [%d]\n",
              exp_req_sz, bhdr->rsize);
}

void assert_free_block_values(bud_free_block *fblk, int exp_order,
                              void *exp_prev_ptr, void *exp_next_ptr)
{
    bud_header *bhdr = &fblk->header;

    cr_assert(bhdr->allocated == UNALLOCATED,
              "header `allocated` bits were not properly set. Expected: [%d] Actual: [%d]\n",
              UNALLOCATED, bhdr->allocated);
    cr_assert(bhdr->order == exp_order,
              "header `order` bits were not properly set. Expected: [%d] Actual: [%d]\n",
              exp_order, bhdr->order);
    cr_assert((void *)fblk->prev == exp_prev_ptr,
              "`prev` pointer was not properly set. Expected: [%p] Actual: [%p]\n",
              exp_prev_ptr, (void *)fblk->prev);
    cr_assert((void *)fblk->next == exp_next_ptr,
              "`next` pointer was not properly set. Expected: [%p] Actual: [%p]\n",
              exp_next_ptr, (void *)fblk->next);
}

// my tests
Test(bud_malloc_suite, malloc_bad_pointers, .init = bud_mem_init, .fini = bud_mem_fini,
     .timeout = 5)
{
    // too small
    errno = 0;
    int *x = bud_malloc(0);
    cr_assert_null(x, "bud_malloc return non null!");
    expect_errno_value(EINVAL);

    // negative
    int *z = bud_malloc(-8);
    cr_assert_null(z, "bud_malloc return non null!");
    expect_errno_value(EINVAL);

    // too large
    int *y = bud_malloc(MAX_BLOCK_SIZE - 7); // 1 byte too large
    cr_assert_null(y, "bud_malloc return non null!");
    expect_errno_value(EINVAL);
}

Test(bud_malloc_suite, malloc_many_small, .init = bud_mem_init, .fini = bud_mem_fini,
     .timeout = 5)
{
    errno = 0;
    // fill heap with min block size blocks
    for (int n = 0; n < MAX_HEAP_SIZE / MIN_BLOCK_SIZE; n++)
    {
        char *x = bud_malloc(sizeof(char *));
        for (int i = 0; i < ORDER_TO_BLOCK_SIZE(ORDER_MIN) - sizeof(bud_header); i++)
        {
            x[i] = 'b';
        }

        cr_assert_not_null(x);

        bud_header *bhdr = PAYLOAD_TO_HEADER(x);
        assert_header_values(bhdr, ALLOCATED, ORDER_MIN, PADDED,
                             sizeof(char *));
        expect_errno_value(0);
    }
    // heap is full
    int *y = bud_malloc(sizeof(int *));
    cr_assert_null(y);
    expect_errno_value(ENOMEM);
}

Test(bud_realloc_suite, realloc_diff_hdr, .init = bud_mem_init, .fini = bud_mem_fini,
     .timeout = 5, .signal = SIGABRT)
{
    errno = 0;
    int *x = bud_malloc(sizeof(int *));

    bud_header *bhdr = PAYLOAD_TO_HEADER(x);
    // change field which should cause abort
    bhdr->padded = 0;

    void *y = bud_realloc(x, 200);
    (void)y;
}

Test(bud_realloc_suite, realloc_end_heap, .init = bud_mem_init, .fini = bud_mem_fini,
     .timeout = 5)
{
    errno = 0;
    // fill heap aside from 64 bytes
    for (int n = 0; n < (MAX_HEAP_SIZE / MIN_BLOCK_SIZE) - 2; n++)
    {
        char *x = bud_malloc(sizeof(char *));
        for (int i = 0; i < ORDER_TO_BLOCK_SIZE(ORDER_MIN) - sizeof(bud_header); i++)
        {
            x[i] = 'b';
        }

        cr_assert_not_null(x);

        bud_header *bhdr = PAYLOAD_TO_HEADER(x);
        assert_header_values(bhdr, ALLOCATED, ORDER_MIN, PADDED,
                             sizeof(char *));
        expect_errno_value(0);
    }
    char *e = bud_malloc(sizeof(char *));
    // only 1 32 block left
    int *y = bud_realloc(e, 25); // ask for 64 block with realloc
    cr_assert_null(y);
    expect_errno_value(ENOMEM);
}
Test(bud_free_suite, malloc_realloc_then_free, .init = bud_mem_init, .fini = bud_mem_fini,
     .timeout = 5)
{
    errno = 0;

    void *a = bud_malloc(4096 - sizeof(bud_header)); // -> 4096
    int *x = bud_malloc(sizeof(int));                // -> MIN_BLOCK_SIZE
    void *b = bud_malloc(sizeof(double) * 2);        // -> MIN_BLOCK_SIZE
    char *y = bud_malloc(sizeof(char) * 100);        // -> 128
    bud_header *bhdr_b = PAYLOAD_TO_HEADER(b);

    assert_header_values(bhdr_b, ALLOCATED, ORDER_MIN, PADDED, sizeof(double) * 2);
    assert_nonempty_free_list(ORDER_MAX - 2);      // this will be used for realloc block e
    bud_free_block *blk = free_list_heads[8].next; // only x is expected on the list
    assert_free_block_values(blk, ORDER_MAX - 2, &free_list_heads[8], &free_list_heads[8]);

    // 0 1 0 1 1 1 1 0 1 0 before
    void *e = bud_realloc(x, ORDER_TO_BLOCK_SIZE(ORDER_MIN + 8) - sizeof(bud_header));
    // 1 1 0 1 1 1 1 0 0 0 after

    assert_empty_free_list(ORDER_MAX - 2); // block e takes this free block

    // this frees x, placing it in freelist[0]
    blk = free_list_heads[0].next; // only x is expected on the list
    assert_nonempty_free_list(ORDER_MIN);
    assert_free_block_values(blk, ORDER_MIN, &free_list_heads[0], &free_list_heads[0]);

    void *z = bud_realloc(e, ORDER_TO_BLOCK_SIZE(ORDER_MIN + 3) - sizeof(bud_header));
    // 1 1 0 2 2 2 2 2 0 0 after
    bud_header *bhdr_z = PAYLOAD_TO_HEADER(z);

    assert_header_values(bhdr_z, ALLOCATED, ORDER_MIN + 3, UNPADDED, ORDER_TO_BLOCK_SIZE(ORDER_MIN + 3) - sizeof(bud_header));

    bud_free(z);
    // 1 1 0 1 1 1 1 0 1 0

    bud_free(y);
    // 1 1 1 1 1 1 1 0 1 0

    bud_free(a);
    // 1 1 1 1 1 1 1 1 1 0

    bud_free(b);
    // 0 0 0 0 0 0 0 0 0 1

    for (int i = 0; i < 9; i++)
    {
        assert_empty_free_list(ORDER_MIN + i);
    }
    assert_nonempty_free_list(ORDER_MIN + 9);

    cr_expect(bud_heap_start() + 1 * MAX_BLOCK_SIZE == bud_heap_end(),
              "Allocated more heap than necessary!");

    expect_errno_value(0);
}
Test(bud_coalesce_suite, invalid_coalesce_pointer,
     .init = bud_mem_init, .fini = bud_mem_fini, .timeout = 5, .signal = SIGABRT)
{
    errno = 0;

    void *a = bud_malloc(4096 - sizeof(bud_header)); // -> 4096
    bud_header *bhdr_a = PAYLOAD_TO_HEADER(a);
    assert_header_values(bhdr_a, ALLOCATED, ORDER_MAX - 3, UNPADDED, 4096 - sizeof(bud_header));

    int *x = bud_malloc(sizeof(int)); // -> MIN_BLOCK_SIZE
    bud_header *bhdr_x = PAYLOAD_TO_HEADER(x);
    assert_header_values(bhdr_x, ALLOCATED, ORDER_MIN, PADDED, sizeof(int));

    void *b = bud_malloc(sizeof(double) * 2); // -> MIN_BLOCK_SIZE
    bud_header *bhdr_b = PAYLOAD_TO_HEADER(b);
    assert_header_values(bhdr_b, ALLOCATED, ORDER_MIN, PADDED, sizeof(double) * 2);

    char *y = bud_malloc(sizeof(char) * 100); // -> 128
    bud_header *bhdr_y = PAYLOAD_TO_HEADER(y);
    assert_header_values(bhdr_y, ALLOCATED, ORDER_MIN + 2, PADDED, sizeof(char) * 100);

    // This block will be split to satisfy the realloc of char *new
    assert_nonempty_free_list(ORDER_MAX - 2);

    char *new = bud_realloc(x, 4095 - sizeof(bud_header));
    // this frees x which is MINBLOCKSIZE
    assert_empty_free_list(ORDER_MAX - 2);
    // x is freed and placed in (ORDER_MIN - 5) INDEX
    assert_nonempty_free_list(ORDER_MIN);

    bud_free_block *blk = free_list_heads[0].next; // only x is expected on the list
    assert_free_block_values(blk, ORDER_MIN, &free_list_heads[0], &free_list_heads[0]);

    bud_header *bhdr_new = PAYLOAD_TO_HEADER(new);
    assert_header_values(bhdr_new, ALLOCATED, ORDER_MAX - 3, PADDED, 4095 - sizeof(bud_header));

    bhdr_new->rsize += 1;
    // this will make padded = false, which will call abort since invalid pointer passed to realloc
    bud_realloc(new, ORDER_TO_BLOCK_SIZE(ORDER_MIN) - sizeof(bud_header));
}

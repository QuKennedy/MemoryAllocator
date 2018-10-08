# MemoryAllocator

This is the core logic for a Buddy System memory allocator written in C. Buddy System allocators maintain a data structure of free blocks sized as powers of 2, which are used to provide best fits for allocation requests, as well as immediate coalescing with neighboring, or "buddy" free blocks of the same size class. This implementation utilizes lists to store free blocks, but the more common and efficient data structure would be a binary tree of free blocks for O(logn) searching rather than linear time.

The pros of the Buddy System are efficient allocates and frees, as well as little external fragmentation due to uniform block sizes. Reducing external fragmentation however comes with the price of increasing internal fragmentation, as requests are not always powers of 2. 


The allocation algorithm is as follows:

A block of memory is requested, and if there is no free block of the preferred size it will iterate until it locates a block, which it will then split repeatedly into 2 unique "buddies" until the left half is the proper size. 
During the split, the right block is placed into the appropriate free list and the left block is checked to see if it is the minimum block size that can satisfy the request, and if so it is allocated. If not, the split algorithm repeats on the left block. 


The coalesce algorithm is as follows:

A buddy block's address can be determined by taking the xor of the current block's pointer and its size, which is due to the addresses of a block and its buddy differing only in the position of their size class. If that block is allocated then return. If not, merge the 2 blocks and repeat the coalesce algorithm with the merged block until an allocated buddy is located.

# MemoryAllocator

This is the core logic for a Binary Buddy System memory allocator written in C. Binary Buddy System allocators maintain a data structure of free blocks sized as powers of 2, which are used to provide first fits for allocation requests, as well as immediate coalescing with neighboring, or "buddy" free blocks of the same size class. This implementation utilizes an array of linked lists to store free blocks, but the more efficient data structure would be a binary tree of free blocks for O(logn) searching rather than linear time.

Pros of the Buddy System are efficient allocates and frees, as well as little external fragmentation due to uniform block sizes. Reducing external fragmentation however comes with the price of increasing internal fragmentation, as requests are not always powers of 2, and in the worst case up to almost half of a block's size can be wasted due to being only a few bytes over the threshold of the next smallest block. 


The allocation algorithm is as follows:

A block of memory is requested, and the number of requested bytes is rounded to the next power of 2. If there is no free block of the preferred size it will iterate over the free list until it locates a block, which it will then split repeatedly into 2 unique "buddies" until these halves are the proper size. 
During the split, the right block is placed into the appropriate free list and the left block is checked to see if it is the minimum block size that can satisfy the request, and if so it is allocated. If not, the split algorithm repeats on the left block. 


The coalesce algorithm is as follows:

A buddy block's address can be determined by taking the xor of the current block's address and its size, which is due to the addresses of a block and its buddy differing only in the bit position of their size class. This does not guarantee that the computed address is a valid buddy block, so the sizes of the block and its potential buddy must match.
If the buddy block is invalid or currently allocated then return. If not, merge the 2 blocks and repeat the coalesce algorithm with the merged block until an invalid or allocated buddy is located.

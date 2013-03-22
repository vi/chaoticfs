#pragma once

/* 
    Block level. 
    
    This level interfaces the file and support the following operations:
    
    1. Allocate random block;
        subject to "reserved space" limitation
    2. Mark the given block as unused;
    3. Mark the given block as used;
    4. Shred the given block;
    5. Read the given block;
    6. Write the given block;
        may also shred some random unused block. Just because.
    
    No crypto is working on this level.
    
    This level is shared between branches of filesystems encrypted with 
    different keys, as long as the block size is the same.
    
    Due to random nature of blocks allocation, it is not possible to tell
    how much data is hidden in "free space" 
    (if the storage file is pre-initialized with random bytes).
*/



struct block_level;

/* Allocate new block_level structure */
struct block_level* block_alloc(void);
    
/* Initialize block_level structure */
int block_init (struct block_level* bl, 
        int block_size,
        int data_fd,
        const char* random_file);

/* Free block_level structure */
void block_free (struct block_level* bl);



/* Allocate a block. Returns -1 on failure */
int block_allocate(struct block_level *bl, int privileged_mode);

void block_mark_used(struct block_level *bl, int i);
void block_mark_unused(struct block_level *bl, int i);
void block_shred(struct block_level *bl, int i);
void block_maybe_shred_some_random(struct block_level *bl);

int block_write(struct block_level *bl, const unsigned char* buffer, int i);
int block_read(struct block_level *bl,        unsigned char* buffer, int i);

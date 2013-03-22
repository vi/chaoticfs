#pragma once

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
void block_mark_used(struct block_level *bl, int i);

int block_write(struct block_level *bl, const unsigned char* buffer, int i);
int block_read(struct block_level *bl,        unsigned char* buffer, int i);

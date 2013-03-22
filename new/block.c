
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include "block.h"


struct block_level {
    /* 0 - free, 1 - busy */
    unsigned char* busy_map;
    unsigned long long busy_blocks_count;
    unsigned long long block_count;
    int block_size;
    
    FILE* random_file;
    int data_fd;
    unsigned char* shred_buffer;
    float reserved_percent;
    int no_shred;
    int readonly_flag;
    int random_shred_probability; /* from 0 to 1000 */
};


static int nearest_power_of_two(int s) {
    int r=1;
    while(r<s) r*=2;
    return r;
}

struct block_level* block_alloc() {
    struct block_level* bl = (struct block_level*) malloc(sizeof (struct block_level));
    if (!bl) return NULL;
    bl->busy_map = NULL;
    bl->random_file = NULL;
    bl->shred_buffer = NULL;
    return bl;
}

void block_free (struct block_level* bl) {
    if(!bl) return;
    free(bl->busy_map);
    if(bl->random_file) { fclose(bl->random_file); }
    free(bl->shred_buffer);
    free(bl);
}

int block_init (struct block_level* bl, 
        int block_size,
        int data_fd,
        const char* random_file) {
    
    bl->block_size = block_size;
    bl->random_file = fopen(random_file, "rb");
    if(!bl->random_file) { 
        perror("fopen random"); 
        return -1; 
    }
    
    bl->data_fd = data_fd;
    if(bl->data_fd <0) {
        perror("invalid data fd");
        return -1;
    }
    
    {
        struct stat st;
        fstat(bl->data_fd,  &st);
        long long int len = st.st_size;
        bl->block_count = (len / block_size);
        if (bl->block_count<1) {
            fprintf(stderr, "Data file is empty. It should be pre-initialized with random data\n");
            return -1;
        }
    }
    
    bl->shred_buffer = (unsigned char*) malloc(bl->block_size);
    bl->busy_map = (unsigned char*) malloc(bl->block_count);
    bl->busy_blocks_count = 0;
    memset(bl->busy_map, 0, bl->block_count);
    
    bl->random_shred_probability=5;
    bl->reserved_percent=5;
    bl->no_shred = 0;
    bl->readonly_flag = 0;
    return 0;
}


/*
   returns -1 on failure 
*/
int block_allocate(struct block_level *bl, int privileged_mode) {
    int i;
    int index=0;
    
    if (!privileged_mode && bl->busy_blocks_count*100.0 >= 
            bl->block_count*(100.0 - bl->reserved_percent)) {
        //fprintf(stderr, "Not priv\n");
        return -1; /* out of free space */
    }
    
    if (bl->busy_blocks_count < bl->block_count - 5) {
        for (i=0; i<100; ++i) {
            unsigned long long int rrr;
            fread(&rrr, sizeof(rrr), 1, bl->random_file);
            index = rrr % bl->block_count;
            if (bl->busy_map[index]) continue;
            bl->busy_map[index] = 1;
            ++bl->busy_blocks_count;
            //fprintf(stderr, "Normal: %d\n", index);
            return index;
        }
    } 
    
    for(i=index+1; i<bl->block_count; ++i) {
        if (bl->busy_map[i]) continue;
        bl->busy_map[i]=1;
        //fprintf(stderr, "Alt1: %d\n", i);
        return i;
    }
    
    for(i=0; i<index; ++i) {
        if (bl->busy_map[i]) continue;
        bl->busy_map[i]=1;
        //fprintf(stderr, "Alt2: %d\n", i);
        return i;        
    }
    
    /* No more free blocks at all */
    
    if (privileged_mode) {
        bl->readonly_flag = 1; // XXX
        /* Emergency measures: expand the storage file to save directory in it */
        fprintf(stderr, "Expanding the data file to store the directory\n");
        ++bl->block_count;
        ++bl->busy_blocks_count;
        int po2 = nearest_power_of_two(bl->block_count);
        bl->busy_map = realloc(bl->busy_map, po2*sizeof(int));
        bl->busy_map[bl->block_count-1]=1;
        fprintf(stderr, "Emeg: %lld\n", bl->block_count-1);
        return bl->block_count-1;
    }
    
    //fprintf(stderr, "Fail\n");
    return -1; /* out of free space */
}

void block_mark_unused(struct block_level *bl, int i) {
    if (!bl->busy_map[i]) {
        fprintf(stderr, "Freeing not occupied block %d\n", i);
    } else {
        --bl->busy_blocks_count;
    }
    bl->busy_map[i] = 0;
}

void block_shred(struct block_level *bl, int i) {
    if (bl->no_shred) return;
    fread(bl->shred_buffer, 1, bl->block_size, bl->random_file);
    block_write(bl, bl->shred_buffer, i);
}

void block_maybe_shred_some_random(struct block_level *bl) {
    unsigned int r;
    int i;
    if (bl->readonly_flag) return;
    fread(&r, 4, 1, bl->random_file);
    r %= 1000;
    if (r < bl->random_shred_probability) {
        int target = -1;
        fread(&r, 4, 1, bl->random_file);
        r %= bl->block_count;
        if (!bl->busy_map[r]) target=r;
        else {
            for(i=(r+1)%bl->block_count; i != r; i=(i+1)%bl->block_count) {
                if (!bl->busy_map[i]) { 
                    target=i;
                    break;
                }
            }
        }
        if (target!= -1) {
            fread(bl->shred_buffer, 1, bl->block_size, bl->random_file);
            block_write(bl, bl->shred_buffer, target);
        }
    }
}

void block_mark_used(struct block_level *bl, int i) {
    if (bl->busy_map[i]) {
        fprintf(stderr, "Marking the block %d twice\n", i);
    } else {
        ++bl->busy_blocks_count;
    }
    bl->busy_map[i] = 1;
}

int block_write(struct block_level *bl, const unsigned char* buffer, int i) {
    int fd = bl->data_fd;
    off_t off = i*bl->block_size;
    size_t s = bl->block_size;
    while(s) {
        int ret = pwrite(fd, buffer, s, off);
        if (ret<=0) {
            if (errno==EINTR || errno==EAGAIN) continue;
            perror("pwrite");
            return 0;
        }
        off+=ret;
        s-=ret;
    }
    block_maybe_shred_some_random(bl);
    return 1;
}


int block_read(struct block_level *bl, unsigned char* buffer, int i) {
    int fd = bl->data_fd;
    off_t off = i*bl->block_size;
    size_t s = bl->block_size;
    while(s) {
        int ret = pread(fd, buffer, s, off);
        if (ret<=0) {
            if (errno==EINTR || errno==EAGAIN) continue;
            return 0;
        }
        off+=ret;
        s-=ret;
    }
    return 1;
}

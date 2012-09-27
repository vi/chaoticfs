// Simple FUSE filesystem that allocates blocks randomly
// Precursor to FUSE-based convenient plausible deniability FS.
// Vitaly "_Vi" Shukela; License=MIT; 2012.

#define FUSE_USE_VERSION 26
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fuse.h>
#include <ulockmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>

#include <mcrypt.h>
#include <mhash.h>

#include <termios.h>


#define SIGNATURE "RndAllV0"
#define BLOCK_HEADER_SIZE 16

int block_count;
int block_size;
FILE* rnd;
int data;
const char* rnd_name;
const char* data_name;

int readonly_flag;
volatile int dirty_status;
volatile int dirty_bytes;
int alarm_triggered;

int max_dirty_bytes;
int max_dirty_calls;
int dirty_alarm_timeout;
int no_shred;
int no_sync;
int reserved_percent;

int user_first_block;

/* 0 - free, 1 - busy */
unsigned char *busy_map;
int busy_blocks_count;

int *saved_directory_blocks;
int saved_directory_blocks_size;

unsigned char* shred_buffer;

struct myblock {
    int num;
    unsigned long iv;
};

struct mydirent {
    char* full_path;
    long long int length;
    struct myblock* blocks;
    int blocks_array_size;
    
};

struct myhandle {
    struct mydirent* ent;
    char* tmpbuf;
    int current_block;   
    int is_dirty; 
};

struct mydirent *dirents;
int current_dirent_array_size;
int dirent_entries_count;

char* mcrypt_algo="rijndael-256";
char* mcrypt_mode="nofb";
int mcrypt_blocksize;
int mcrypt_ivsize;
int mcrypt_keysize=256;

int random_shred_probability=5;
// of 1000

int hash_algo = MHASH_SHA256;
int keygen_algo = KEYGEN_S2K_ISALTED;
int keygen_count= 190;
char* keygen_salt = "RandomAllocFS_m2slLmqisccCaqnzpwkkkemsdffnqpalstteqkleeqwelfs";
char* mcrypt_initvect=
    "\x1d\xcb\x85\x06\xd0\x55\x95\xbd\xbd\xc6\xc2\x97\xa5\x72\xff\x1b\x4e\x0b\x86\x0c\x88\x41\xa3\xc2\xab\x4a\x92\xaf\xf0\x52"
    "\x00\x59\xa3\x45\xa0\xb5\x79\x40\xaa\x9c\xd1\x11\x25\x61\xaf\x5b\xca\xce\xed\xbb\x32\xc2\x1c\x2d\xe8\xd0\xff\x0f\x50\xa2"
    "\x80\xc0\xc2\x12\x5d\x36\xc5\x49\x99\xab\x2d\xef\xd7\x87\x3d\x50\x35\x91\xcc\x0d\x42\x07\x76\x86\xbe\x00\x9a\xf0\xf2\x09"
    "\xbe\x0c\x88\xda\x0f\x44\x13\x9a\x9b\x8c\x5f\x90\x38\x42\x09\xdf\x65\xf3\xf5\x43\x5a\xbb\x78\x55\xbb\xfa\x4a\x2c\x50\xe7"
    "\x75\x56\x39\xcd\x6c\x46\x59\x24\xa6\xb0\xad\x39\x39\xe3\x9b\x32\xaa\x70\xd1\x4d\x68\x5f\xfa\x59\x72\x8a\x2f\xc4\x8b\x63"
    "\xd3\x2e\xf2\x4d\x86\x16\x7a\x4c\x1c\x3f\x83\x46\xea\xb3\xf7\xb7\x0a\xc9\x64\x8e\x14\xe1\x2b\x37\x7b\xc1\xb6\x60\x16\x04"
    "\x06\xf6\xbd\xef\xec\x96\x17\x42\xbf\x5b\x97\x94\x7e\x68\xfd\xa6\xbe\xab\xa5\xd4\x72\x35\x61\x1b\xad\x61\x95\x47\xce\xb2"
    "\xd3\xf5\x8d\x24\xca\xb6\x83\x42\xb6\xe1\xf6\xcf\xcc\x9c\xa0\x23\x04\x8a\x14\xd7\xc9\x4f\xfe\x80\x1d\xaf\xd2\x04\x3c\xde"
    "\x44\xd4\x6f\xa8\xc5\xd7\xf5\x93\xe4\x6f\xd4\xe9\xa9\xc9\x18\x8a\xf1\xb3\xba\x5b\x7a\x00\x77\xb0\x6b\xb5\xa8\xf1\x18\xdd"
    "\x79\x09\xb5\xf4\x53\xf4\x1d\x4b\x38\x60\xb0\x47\x36\xe3\x15\x56\x52\x2d\x7b\xa6\x19\x1e\x08\xe7\x87\x2a\xbb\x6e\xe5\x4f"
    ;
char* mcrypt_key = NULL;
unsigned char* mcrypt_buf = NULL;
unsigned char* mcrypt_ivbuf = NULL;

        
MCRYPT mcrypt = MCRYPT_FAILED;

int is_directory(const struct mydirent* i) {
    return i->full_path[strlen(i->full_path)-1] == '/';
}

int is_file(const struct mydirent* i) {
    return ! is_directory(i);
}

struct mydirent* find_dirent(const char* path) {
    int i;
    int pl = strlen(path);
    if (pl==0) return NULL;
    if (path[pl-1]=='/') --pl;
    for (i=0; i<dirent_entries_count; ++i) {
        int l = strlen(dirents[i].full_path);
        if (is_directory(&dirents[i])) --l;
        
        if (pl != l) continue;
        
        if (strncmp(path, dirents[i].full_path, pl)) continue;
            
        return &dirents[i];
    }
    return NULL;
}

void copy_dirent(struct mydirent* dst, struct mydirent* src) {
    memcpy(dst, src, sizeof (*src));
}

int get_block_count_for_length(long long int size);
void mark_unused_block(int i);
int write_block(const unsigned char* buffer, struct myblock *block);
int write_block_ll(const unsigned char* buffer, int i);
int nearest_power_of_two(int s);
void shred_block(int i);

void remove_dirent(struct mydirent* ent) {
    int index = ent - dirents;
    int i;
    
    free(ent->full_path);
    
    unsigned char* zeroes = (unsigned char*) malloc(block_size);
    memset(zeroes, 0, block_size);
    
    if (ent->blocks) {
        int bc = get_block_count_for_length(ent->length);
        for (i=0; i<bc; ++i) {
            shred_block(ent->blocks[i].num);
            mark_unused_block(ent->blocks[i].num);
        }
    }
    free(ent->blocks);
    free(zeroes);
    for(i=index; i<dirent_entries_count-1; ++i) {
        copy_dirent(&dirents[i], &dirents[i+1]);
    }
    --dirent_entries_count;
}

int get_maximum_path_length();

struct mydirent* create_dirent(const char* path) {
    if (strlen(path) > get_maximum_path_length()-12) {
        return NULL;
    }
    
    struct mydirent* ent;
    if (dirent_entries_count < current_dirent_array_size) {
        ent = &dirents[dirent_entries_count++];
    } else {
        current_dirent_array_size*=2;
        dirents = realloc(dirents, current_dirent_array_size*sizeof(*dirents));
        ent = &dirents[dirent_entries_count++];
    }
    ent->full_path = strdup(path);
    ent->length = 0;
    ent->blocks_array_size = 0;
    ent->blocks = NULL;
    return ent;
}


/*
   returns -1 on failure 
*/
int allocate_block(int privileged_mode) {
    int i;
    int index=0;
    
    if (!privileged_mode && busy_blocks_count*100.0 >= block_count*(100.0-reserved_percent)) {
        //fprintf(stderr, "Not priv\n");
        return -1; /* out of free space */
    }
    
    if (busy_blocks_count < block_count - 5) {
        for (i=0; i<100; ++i) {
            unsigned long long int rrr;
            fread(&rrr, sizeof(rrr), 1, rnd);
            index = rrr % block_count;
            if (busy_map[index]) continue;
            if (index == user_first_block) {
                fprintf(stderr, "Starting block is not used?\n");
                continue;
            }
            busy_map[index] = 1;
            ++busy_blocks_count;
            //fprintf(stderr, "Normal: %d\n", index);
            return index;
        }
    } 
    
    for(i=index+1; i<block_count; ++i) {
        if (busy_map[i]) continue;
        busy_map[i]=1;
        //fprintf(stderr, "Alt1: %d\n", i);
        return i;
    }
    
    for(i=0; i<index; ++i) {
        if (busy_map[i]) continue;
        busy_map[i]=1;
        //fprintf(stderr, "Alt2: %d\n", i);
        return i;        
    }
    
    /* No more free blocks at all */
    
    if (privileged_mode) {
        readonly_flag = 1;
        /* Emergency measures: expand the storage file to save directory in it */
        fprintf(stderr, "Expanding the data file to store the directory\n");
        ++block_count;
        ++busy_blocks_count;
        int po2 = nearest_power_of_two(block_count);
        busy_map = realloc(busy_map, po2*sizeof(int));
        busy_map[block_count-1]=1;
        fprintf(stderr, "Emeg: %d\n", block_count-1);
        return block_count-1;
    }
    
    //fprintf(stderr, "Fail\n");
    return -1; /* out of free space */
}

void mark_unused_block(int i) {
    if (!busy_map[i]) {
        fprintf(stderr, "Freeing not occupied block %d\n", i);
    } else {
        --busy_blocks_count;
    }
    busy_map[i] = 0;
}

void shred_block(int i) {
    if (no_shred) return;
    fread(shred_buffer, 1, block_size, rnd);
    write_block_ll(shred_buffer, i);
}

void maybe_shred_some_random_block() {
    unsigned int r;
    int i;
    if (readonly_flag) return;
    fread(&r, 4, 1, rnd);
    r %= 1000;
    if (r<random_shred_probability) {
        int target = -1;
        fread(&r, 4, 1, rnd);
        r %= block_count;
        if (!busy_map[r]) target=r;
        else {
            for(i=(r+1)%block_count; i != r; i=(i+1)%block_count) {
                if (!busy_map[i]) { 
                    target=i;
                    break;
                }
            }
        }
        if (target!= -1) {
            fread(shred_buffer, 1, block_size, rnd);
            write_block_ll(shred_buffer, target);
        }
    }
}

void mark_used_block(int i) {
    if (busy_map[i]) {
        fprintf(stderr, "Marking the block %d twice\n", i);
    } else {
        ++busy_blocks_count;
    }
    busy_map[i] = 1;
}

int nearest_power_of_two(int s) {
    int r=1;
    while(r<s) r*=2;
    return r;
}

int get_block_count_for_length(long long int size) {
    int bc = (size - 1) / block_size + 1;
    if (size == 0) bc = 0;
    return bc;
}

/* returns 0 on failure, 1 on success */
int ensure_size(struct mydirent* ent, long long int size) {
    if (size == 0) return 1;
    if (size <= ent->length) return 1;
    int ent_block_count      = get_block_count_for_length(ent->length);
    int required_block_count = get_block_count_for_length(size);
    if (required_block_count > ent->blocks_array_size) {
        ent->blocks_array_size = nearest_power_of_two(required_block_count);
        struct myblock* nb = (struct myblock*)realloc(ent->blocks, ent->blocks_array_size*sizeof(*ent->blocks));
        if(!nb) return 0;
        ent->blocks = nb;
    }
    
    unsigned char* zeroes = (unsigned char*) malloc(block_size);
    memset(zeroes, 0, block_size);
    
    int i;
    for(i=ent_block_count; i<required_block_count; ++i) {
        ent->blocks[i].num = allocate_block(0);
        ent->blocks[i].iv = 0;
        if ((ent->blocks[i].num) == -1) {
            free(zeroes);
            return 0;
        }
        write_block(zeroes, &ent->blocks[i]);
    }
    free(zeroes);
    
    ent->length = size;
    return 1;    
}

int d_truncate(struct mydirent* ent, long long int size) {
    if (size >= ent->length) return ensure_size(ent, size);
        
    int ent_block_count      = get_block_count_for_length(ent->length);
    int required_block_count = get_block_count_for_length(size);
    int i;
    
    for (i=required_block_count; i<ent_block_count; ++i) {
        shred_block(ent->blocks[i].num);
        mark_unused_block(ent->blocks[i].num);
    }
    ent->length = size;
    return 1;
}

int write_block_ll(const unsigned char* buffer, int i) {
    int fd = data;
    off_t off = i*block_size;
    size_t s = block_size;
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
    maybe_shred_some_random_block();
    return 1;
}

static __attribute__((const)) int imin(int a, int b) { return (a < b) ? a : b; }

int write_block(const unsigned char* buffer, struct myblock *block) {
    int i = block->num;
    memcpy(mcrypt_buf, buffer, block_size);
    if (mcrypt == MCRYPT_FAILED) {
        return write_block_ll(mcrypt_buf, i);
    } else {
        fread(&block->iv, 1, sizeof(block->iv), rnd);
        int s = imin(sizeof(block->iv), mcrypt_ivsize);
        memset(mcrypt_ivbuf, 0, mcrypt_ivsize);
        memcpy(mcrypt_ivbuf, &block->iv, s);
        
        if(mcrypt_generic_init(mcrypt, mcrypt_key, mcrypt_keysize/8, mcrypt_ivbuf) < 0) {
            fprintf(stderr, "Encryption init error\n");
            return 0;
        }
        if(mcrypt_generic (mcrypt, mcrypt_buf, block_size) < 0) {
            fprintf(stderr, "Encryption error\n");
            return 0;
        }
        mcrypt_generic_deinit(mcrypt);
        
        return write_block_ll(mcrypt_buf, i);
    }
}

int write_block_simple(const unsigned char* buffer, int i) {
    struct myblock b;
    b.num = i;
    b.iv = htobe32(i);
    return write_block(buffer, &b);
}

int read_block_ll(unsigned char* buffer, int i) {
    int fd = data;
    off_t off = i*block_size;
    size_t s = block_size;
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

int read_block(unsigned char* buffer, struct myblock *block) {
    int i = block->num;
    int ret = read_block_ll(mcrypt_buf, i);
    if (!ret) return 0;
    if (mcrypt == MCRYPT_FAILED) {
        memcpy(buffer, mcrypt_buf, block_size);   
    } else {
        int ret = read_block_ll(mcrypt_buf, i);
        if (!ret) return 0;
        
        int s = imin(sizeof(block->iv), mcrypt_ivsize);
        memset(mcrypt_ivbuf, 0, mcrypt_ivsize);
        memcpy(mcrypt_ivbuf, &block->iv, s);
        
        if(mcrypt_generic_init(mcrypt, mcrypt_key, mcrypt_keysize/8, mcrypt_ivbuf) < 0) return 0;
        if(mdecrypt_generic (mcrypt, mcrypt_buf, block_size) < 0) return 0;
        mcrypt_generic_deinit(mcrypt);
    }
    memcpy(buffer, mcrypt_buf, block_size);    
    return 0;
}

int read_block_simple(unsigned char* buffer, int i) {
    struct myblock b;
    b.num = i;
    b.iv = htobe32(i);
    return read_block(buffer, &b);
}

int get_maximum_path_length() {
    size_t dirent_size = 0;
    dirent_size += 4; /* full_path string length */
    dirent_size += 8; /* file length */
    //int bc = get_block_count_for_length(ent->length);
    //dirent_size += 4*bc; /* block list */
    
    dirent_size += 4; /* number of blocks in this extent */
    dirent_size += 4; /* starting block in this extend */
    // If we can't save all block numbers in this block, we save further block numbers in next blocks
    
    dirent_size+=16; /* there should be room for at least 2 blocks or this is not serious */
    dirent_size += 4; /* next dirent's block number */
    dirent_size += 4; /* next dirent's offset in block */ 
    dirent_size += 8; /* padding for possible extensions */
    return block_size - BLOCK_HEADER_SIZE - dirent_size;
}

int get_saved_entry_minimal_size(struct mydirent* ent) {
    size_t dirent_size = 0;
    dirent_size += 4; /* full_path string length */
    dirent_size += strlen(ent->full_path);
    dirent_size += 8; /* file length */
    //int bc = get_block_count_for_length(ent->length);
    //dirent_size += 4*bc; /* block list */
    
    dirent_size += 4; /* number of blocks in this extent */
    dirent_size += 4; /* starting block in this extend */
    // If we can't save all block numbers in this block, we save further block numbers in next blocks
    
    dirent_size+=16; /* there should be room for at least 2 blocks or this is not serious */
    dirent_size += 4; /* next dirent's block number */
    dirent_size += 4; /* next dirent's offset in block */ 
    dirent_size += 8; /* padding for possible extensions */
    return dirent_size;
}

/* Returns first entry's block. -1 on failure */
int save_entries(int starting_block) {
    int i, j;
    
    if (!dirty_status) {
        return starting_block;
    }
    
    /* Need to do this early to prevent stray SIGALRM re-enter save_entries */
    dirty_status=0;
    dirty_bytes=0;
    
    
    
    int allocated_blocks_journal_size=32;
    int *allocated_blocks_journal = (int*) malloc(allocated_blocks_journal_size*sizeof(int));
    int number_of_allocated_blocks=0;
    
    int first_block = starting_block;
    if(!first_block==-1) {
        free(allocated_blocks_journal);
        return -1;
    }
    allocated_blocks_journal[number_of_allocated_blocks++] = first_block;
    
    int current_block = first_block;
    
    /* First block is saved last to prevent entirely corrupting the filesystem in case of sudden shutdown */
    unsigned char* first_block_buffer = (unsigned char*) malloc(block_size);
    unsigned char* block_buffer = (unsigned char*) malloc(block_size);
    unsigned char* block = first_block_buffer;
    fread(block, 1, 8, rnd);
    memcpy(block+8, SIGNATURE, 8);
    int offset = BLOCK_HEADER_SIZE; 
    
    int next_dirent_size = 0;
    next_dirent_size = get_saved_entry_minimal_size(&dirents[0]);
    
    int position_in_block_list = 0;
    int dirent_fully_saved = 0;
    
    for (i=0; i<dirent_entries_count; ++i) {
        struct mydirent* ent = &dirents[i];
        
        int current_dirent_size = next_dirent_size;
        //fprintf(stderr, "i=%d bs=%d off=%d crs=%d\n", i, block_size, offset, current_dirent_size);
        if (block_size - offset - current_dirent_size<4) {
            fprintf(stderr, "Filepath too long for this block size and will be skipped\n");
             if (i==dirent_entries_count-1) {
                break;
            } else {
                next_dirent_size = get_saved_entry_minimal_size(&dirents[i+1]);
            }
            continue;
        }
        int number_of_blocks_we_will_save = (block_size - offset - current_dirent_size) / sizeof(struct myblock);
        if (i==dirent_entries_count-1) {
            next_dirent_size = 0;
        } else {
            next_dirent_size = get_saved_entry_minimal_size(&dirents[i+1]);
        }
        //fprintf(stderr, "nds=%d\n", next_dirent_size);
        
        int path_string_length = strlen(ent->full_path);
        long long int file_lenght = ent->length;
        int bc = get_block_count_for_length(ent->length);
        
        if (bc <= position_in_block_list + number_of_blocks_we_will_save) {
            number_of_blocks_we_will_save = bc - position_in_block_list;
            dirent_fully_saved = 1;
        } else {
            dirent_fully_saved = 0;
            next_dirent_size = current_dirent_size;
        }
        
        *(long int*)(block+offset) = htobe32(path_string_length); offset+=4;
        memcpy(block+offset, ent->full_path, path_string_length); offset+=path_string_length;
        *(long long int*)(block+offset) = htobe64(file_lenght); offset+=8;
        *(long int*)(block+offset) = htobe32(number_of_blocks_we_will_save); offset+=4;
        *(long int*)(block+offset) = htobe32(position_in_block_list); offset+=4;
        for (j=position_in_block_list; j<position_in_block_list + number_of_blocks_we_will_save; ++j) {
            *(long int*)(block+offset) = htobe32(ent->blocks[j].num); offset+=4;
            *(long int*)(block+offset) = htobe32(ent->blocks[j].iv); offset+=4;
        }
        position_in_block_list += number_of_blocks_we_will_save;
        //fprintf(stderr, "Offset: %d\n", offset);
        if (next_dirent_size == 0) {
            *(long int*)(block+offset) = htobe32(0); offset+=4;
            *(long int*)(block+offset) = htobe32(0); offset+=4;
            memset(block+offset, 0, 8); offset+=8;            
        } else if(offset+16+next_dirent_size < block_size) {
            *(long int*)(block+offset) = htobe32(current_block); offset+=4;
            *(long int*)(block+offset) = htobe32(offset+12); offset+=4;
            memset(block+offset, 0, 8); offset+=8;            
        } else {
            int new_block = allocate_block(1);
            if(new_block==-1) {                
                free(first_block_buffer);
                free(block_buffer);
                /* rolling back block allocations... */
                for (j=0; j<number_of_allocated_blocks; ++j) {
                    if (allocated_blocks_journal[i]!=starting_block) {
                        mark_unused_block(allocated_blocks_journal[i]);
                    }
                }
                free(allocated_blocks_journal);
                return -1;
            } else {
                if (allocated_blocks_journal_size == number_of_allocated_blocks) {
                   allocated_blocks_journal_size*=2;
                   allocated_blocks_journal = (int*)realloc(allocated_blocks_journal,
                        allocated_blocks_journal_size*sizeof(int));
                }
                allocated_blocks_journal[number_of_allocated_blocks++] = new_block;
            }
            *(long int*)(block+offset) = htobe32(new_block); offset+=4;
            *(long int*)(block+offset) = htobe32(BLOCK_HEADER_SIZE); offset+=4;
            memset(block+offset, 0, 8); offset+=8;            
            
            if (block == first_block_buffer) {
                block = block_buffer;
            } else {
                write_block_simple(block, current_block);
            }
            
            current_block = new_block;
            fread(block, 1, 8, rnd);
            memcpy(block+8, SIGNATURE, 8);
            offset = BLOCK_HEADER_SIZE; 
        }
        if (dirent_fully_saved) {
            position_in_block_list = 0;
        } else {
            --i;
        }
    }
    /*
    fprintf(stderr, "Dir blocks:");
    for(i=0; i<saved_directory_blocks_size; ++i) {
        fprintf(stderr, " %d", saved_directory_blocks[i]);
    }
    fprintf(stderr, "\n");
    */
    
    write_block_simple(block, current_block);
    write_block_simple(first_block_buffer, starting_block);
    
    if (!no_sync) {
        fdatasync(data);
    }
    
    for (i=0; i<saved_directory_blocks_size; ++i) {
        if (saved_directory_blocks[i]!=starting_block) {
            mark_unused_block(saved_directory_blocks[i]);
        }
    }
    saved_directory_blocks_size = number_of_allocated_blocks;
    free(saved_directory_blocks);
    saved_directory_blocks = (int*)malloc(saved_directory_blocks_size*sizeof(int));
    memcpy(saved_directory_blocks, allocated_blocks_journal, sizeof(int)*number_of_allocated_blocks);
    
    /*
    fprintf(stderr, "Dir blocks:");
    for(i=0; i<saved_directory_blocks_size; ++i) {
        fprintf(stderr, " %d", saved_directory_blocks[i]);
    }
    fprintf(stderr, "\n");
    */
    
    free(first_block_buffer);
    free(block_buffer);
    free(allocated_blocks_journal);
    return first_block;
}

/* return number of loaded entries on success, 0 on failure */
int load_entries(int starting_block, int only_mark_blocks) {
    if (!busy_map[starting_block]) {
        mark_used_block(starting_block);
    }
    int current_block = starting_block;
    
    unsigned char* block = (unsigned char*) malloc(block_size);
    
    read_block_simple(block, starting_block);
    int offset;
    if (memcmp(block+8, SIGNATURE, 8)) {
        return 0;
    }
    offset=BLOCK_HEADER_SIZE;
    
    int j;
    int counter;
    
    char* previous_entry_name = strdup("///"); /* non-existing name */
    
    struct mydirent *ent = NULL;
            
    for(;;) {            
        int pathlen = be32toh(*(long int*)(block+offset)); offset+=4;
        if(pathlen < 0 || pathlen >= block_size-32) { free(block); return 0; }
        if (!only_mark_blocks) {
            char* path = strndup((char*)(block+offset), pathlen);
            if (!strcmp(previous_entry_name, path)) {
                /* continued blocks for old entry, not a new one */
            } else {
                free(previous_entry_name);
                previous_entry_name = path;
                ent = create_dirent(path);
                if (!ent) {
                    fprintf(stderr, "Entry name too long and ignored\n");
                }
            }
        }
        offset+=pathlen;
        long long int filelen = be64toh(*(long long int*)(block+offset)); offset+=8;
        if (ent) {
            ent->length = filelen;
        }
        
        int bc = get_block_count_for_length(filelen);
        int blocks_here = be32toh(*(long int*)(block+offset)); offset+=4;
        int position_in_block_list = be32toh(*(long int*)(block+offset)); offset+=4;
        if (ent && !ent->blocks && bc) {
            ent->blocks_array_size = nearest_power_of_two(bc);
            ent->blocks = (struct myblock*)malloc(ent->blocks_array_size * sizeof(*ent->blocks));
            memset(ent->blocks, 0, ent->blocks_array_size);
        }
        for (j=0; j<blocks_here; ++j) {
            int idx = be32toh(*(long int*)(block+offset)); offset+=4;
            unsigned long iv = be32toh(*(long int*)(block+offset)); offset+=4;
            if(idx>=0 && idx<block_count) {
                mark_used_block(idx);
                if (ent) {
                    ent->blocks[j+position_in_block_list].num = idx;
                    ent->blocks[j+position_in_block_list].iv = iv;
                }
            } else {
                free(block);
                return 0;
            }
        }
        
        ++counter;
        
        int next_block = be32toh(*(long int*)(block+offset)); offset+=4;
        int next_offset = be32toh(*(long int*)(block+offset)); offset+=4;
        
        if (next_block == 0 && next_offset == 0) break;
        
        if (next_block < 0 || next_block >= block_count) { free(block); return 0; }
        if (next_offset < BLOCK_HEADER_SIZE || next_offset >= block_size-32) { free(block); return 0; }
            
        if (next_block != current_block) {
            current_block = next_block;
            read_block_simple(block, current_block);
            if (memcmp(block+8, SIGNATURE, 8)) {
                fprintf(stderr, "Signature failed in loading block\n");
                return counter;
            }
            mark_used_block(current_block);
        }        
        
        offset = next_offset;
    }
    
    free(block);
    free(previous_entry_name);
    return counter;
}

/* return 1 on success, 0 on failure */
void traverse_entries_and_debug_print(int starting_block) {
    int current_block = starting_block;
    
    unsigned char* block = (unsigned char*) malloc(block_size);
    unsigned char* block2 = (unsigned char*) malloc(block_size);
    
    read_block_simple(block, starting_block);
    int offset;
    {
        if (memcmp(block+8, SIGNATURE, 8)) {
            char buf[10];
            snprintf(buf, 9, "%s", block+8);
            printf("Block signature is %s instead of %s\n", buf, SIGNATURE);
        }
    }
    offset=BLOCK_HEADER_SIZE;
    
    int j;
    
    for(;;) {
        int pathlen = be32toh(*(long int*)(block+offset)); offset+=4;
        if(pathlen < 0 || pathlen >= block_size-32) {
            fprintf(stderr, "pathlen = %d is too big\n", pathlen);
            free(block2); free(block);
            return;
        }
        {
            unsigned char buf[256];
            memcpy(buf, block+offset, pathlen);
            buf[pathlen]=0;
            fprintf(stdout, "entry %s\n", buf); fflush(stdout);
        }
        offset+=pathlen;
        long long int filelen = be64toh(*(long long int*)(block+offset)); offset+=8;
        fprintf(stdout, "  size %lld (", filelen); fflush(stdout);
        int bc = get_block_count_for_length(filelen);
        fprintf(stdout, "block_count %d)\n", bc); fflush(stdout);
        int blocks_here = be32toh(*(long int*)(block+offset)); offset+=4;
        fprintf(stdout, "  block here %d\n", blocks_here); fflush(stdout);
        int blocks_offset = be32toh(*(long int*)(block+offset)); offset+=4;
        fprintf(stdout, "  blocks offset %d\n", blocks_offset); fflush(stdout);
        for (j=0; j<blocks_here; ++j) {
            int idx = be32toh(*(long int*)(block+offset)); offset+=4;
            int iv = be32toh(*(long int*)(block+offset)); offset+=4;
            fprintf(stdout, "  block %d iv %08X\n", idx, iv); fflush(stdout);
            if(idx>=0 && idx<block_count) {
                struct myblock b;
                b.num = idx;
                b.iv = iv;
                read_block(block2, &b);
                fprintf(stdout, "    %02X%02X%02X%02X\n", 
                    block2[0], block2[1], block2[2], block2[3]); 
                fflush(stdout);
            }
        }
        int next_block = be32toh(*(long int*)(block+offset)); offset+=4;
        fprintf(stdout, "  next_block %d\n", next_block); fflush(stdout);
        int next_offset = be32toh(*(long int*)(block+offset)); offset+=4;
        fprintf(stdout, "  next_offset %d\n", next_offset); fflush(stdout);
        
        if (next_block == 0 && next_offset == 0) break;
        
        if (next_block < 0 || next_block >= block_count) { free(block); free(block2); return; }
        if (next_offset < 8  || next_offset >= block_size-32) { free(block); free(block2); return; }
            
        if (next_block != current_block) {
            current_block = next_block;
            read_block_simple(block, current_block);
            if (memcmp(block+8, SIGNATURE, 8)) {
                char buf[10];
                snprintf(buf, 9, "%s", block+8);
                printf("Block signature is %s instead of %s\n", buf, SIGNATURE);
            }
        }        
        
        offset = next_offset;
    }
    
    free(block);
    free(block2);
    return;
}

void generate_test_dirents() {
    struct mydirent* ent;

    ent = create_dirent("/");
    
    ent = create_dirent("/ololo");    
    ensure_size(ent, 20);
    
    ent = find_dirent("/ololo");
    unsigned char *block = (unsigned char*) malloc(block_size);
    strcpy((char*)block, "Hello, world\n");
    write_block(block, &ent->blocks[0]);
    
    ent = create_dirent("/r/");
    
    
    ent = create_dirent("/r/ke");
    
    ent = create_dirent("/r/kekeke");
    ensure_size(ent, 10000);
    int i;
    for(i=0; i<9999/block_size+1; ++i) {
        block[1]=i;
        write_block(block, &ent->blocks[i]);
    }
    
    
    free(block);
    
    dirty_status=1;
    
    int s = save_entries(user_first_block);
    
    fprintf(stderr, "%d\n", s);
}

void raise_alarm() {
    if(!alarm_triggered) {
        alarm(dirty_alarm_timeout);
        alarm_triggered=1;
    }
}

void debug_print_dirents(int starting_block) {
    traverse_entries_and_debug_print(starting_block);
    load_entries(starting_block, 1);
    int i;
    fprintf(stdout, "busy blocks: ");
    for(i=0; i<block_count; ++i) {
        if (busy_map[i]) {
            fprintf(stdout, "%d ", i);
        }
    }
    fprintf(stdout, "\n"); fflush(stdout);
    fprintf(stdout, "usage: %d of %d (%g%%)\n", busy_blocks_count, block_count, 100.0*busy_blocks_count/block_count);
}


static int xmp_getattr(const char *path, struct stat *stbuf)
{
    struct mydirent* ent = find_dirent(path);
    if(!ent) return -ENOENT;
        
    
    memset(stbuf, 0, sizeof(*stbuf));
    if (is_directory(ent)) {
        stbuf->st_mode = 0750 | S_IFDIR;
    } else {
        stbuf->st_mode = 0750 | S_IFREG;
        stbuf->st_size = ent->length;
        stbuf->st_blocks = get_block_count_for_length(ent->length);
        stbuf->st_blksize = block_size;
    }
    stbuf->st_ino = ent - dirents;
    return 0;
}

static int xmp_access(const char *path, int mask)
{
        return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size) { return -ENOSYS; }
static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
    struct stat st;
    //struct mydirent* ent = find_dirent(path);
        
    //if(!ent) return -ENOENT;
    //if(!is_directory(ent)) return -ENOENT;
    
    memset(&st, 0, sizeof(st));
    st.st_ino = 0;
    st.st_mode = 0640 | S_IFDIR;
    filler(buf, ".", &st, 0);
    filler(buf, "..", &st, 0);
    
    int i;
    int l = strlen(path);
    if (path[l-1]=='/') --l;
    
    // suppose path is "/ololo"
    for (i=0; i<dirent_entries_count; ++i) {
        struct mydirent* ent = &dirents[i];
        if(!strncmp(path, ent->full_path, l)) {
            // /ololoWHATEVER
            if (!strcmp(ent->full_path+l, "/")) continue; //  /ololo/ itself
            if (ent->full_path[l] != '/') continue; // /ololo2
                
            char pbuf[256];
            strncpy(pbuf, ent->full_path+l+1, 256);
            pbuf[255]=0;
            
            if (strchr(ent->full_path+l+1, '/')) {
                // /ololo/*/*
                if (strcmp(strchr(ent->full_path+l+1, '/'), "/")) {
                    // /ololo/something/nested
                    continue; // in subdirectory
                } else {
                    // /ololo/something/
                    // just directory mydirent
                }
            } 
            
            if (is_directory(ent)) {                
                st.st_mode = 0750 | S_IFDIR;
                pbuf[strlen(pbuf)-1]=0; // strip trailing '/'
            } else {
                st.st_mode = 0750 | S_IFREG;
                st.st_size = ent->length;
                st.st_blocks = get_block_count_for_length(ent->length);
                st.st_blksize = block_size;
            }
            st.st_ino = i;
            if (filler(buf, pbuf, &st, 0)) {
                return 0;
            }
        }
    }
    
    return 0;
}
static int xmp_mkdir(const char *path, mode_t mode)
{
    if(readonly_flag) return -EROFS;
    struct mydirent* ent = find_dirent(path);
    if(ent) return -EEXIST;
    
    int l = strlen(path);
    
    if(l>PATH_MAX-2) return -ENOSYS;
        
    ++dirty_status; raise_alarm();
    
    char buf[PATH_MAX];
    strncpy(buf, path, PATH_MAX);
    buf[PATH_MAX-1]=0;
    
    // ensure the path ends in trailing slash
    if(buf[l-1]!='/') buf[l]='/'; 
    
    ent = create_dirent(buf);
    
    return ent?0:-ENAMETOOLONG;
}

static int xmp_unlink(const char *path)
{
    if(readonly_flag) return -EROFS;
    struct mydirent* ent = find_dirent(path);
    if (!ent) return -ENOENT;
    if (is_directory(ent)) return -EISDIR;
    
    remove_dirent(ent);
    ++dirty_status; raise_alarm();
    
    return 0;
}

static int xmp_rmdir(const char *path)
{
    if(readonly_flag) return -EROFS;
    struct mydirent* ent = find_dirent(path);
    if (!ent) return -ENOENT;
    if (!is_directory(ent)) return -ENOTDIR;
    
    int l = strlen(path);
    if (path[l-1]=='/') --l;
    int i;
    for (i=0; i<dirent_entries_count; ++i) {
        struct mydirent* ent2 = &dirents[i];
        if(!strncmp(path, ent2->full_path, l)) {
            // /ololoWHATEVER
            if (ent2 == ent) continue; //  /ololo/ itself
            if (ent->full_path[l] != '/') continue; // /ololo2
                
            return -ENOTEMPTY;
        }   
    }
    
    remove_dirent(ent);
    ++dirty_status; raise_alarm();
    
    return 0;
}
static int xmp_rename(const char *from, const char *to)
{
    if(readonly_flag) return -EROFS;
    struct mydirent* ent = find_dirent(from);
    struct mydirent* ent2 = find_dirent(to);
        
    if(!ent) return -ENOENT;
    if(ent2) return -ENOTEMPTY;
    
    if(strlen(to) > get_maximum_path_length()-12) return -ENAMETOOLONG;
    ++dirty_status; raise_alarm();
    
    int l = strlen(to);
    
    if(l>PATH_MAX-2) return -ENOSYS;
    
    char buf[PATH_MAX];
    strncpy(buf, to, PATH_MAX);
    buf[PATH_MAX-1]=0;
    
    if(is_directory(ent)) {
        // ensure the path ends in trailing slash
        if(buf[l-1]!='/') buf[l]='/'; 
    } else {
        // ensure that file path has not trailing slash
        if(buf[l-1]=='/') buf[l-1]=0;
    }
    
    free(ent->full_path);
    ent->full_path = strdup(buf);
    
    
    return 0;
}
static int xmp_chmod(const char *path, mode_t mode) { return 0; }
static int xmp_chown(const char *path, uid_t uid, gid_t gid) { return 0; }

static int xmp_truncate(const char *path, off_t size)
{
    if(readonly_flag) return -EROFS;
    struct mydirent* ent = find_dirent(path);
    if (!ent) return -ENOENT;   
        
    int ret = d_truncate(ent, size);
    ++dirty_status; raise_alarm();
    if(ret) return 0;
        
	return -ENOSPC;
}

static int xmp_utimens(const char *path, const struct timespec ts[2]) { return 0; }


static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	struct mydirent* ent = find_dirent(path);
    
    if (ent && is_directory(ent)) return -EISDIR;
        
    int flags = fi->flags;
    
    if (flags&O_CREAT) {
        if (ent) {
            if (flags&O_EXCL) return -EEXIST;
            if (flags&O_TRUNC) {
                d_truncate(ent, 0);
            }
        } else {
            ent = create_dirent(path);
            if(!ent) return -ENAMETOOLONG;
        }
    } else {
        if (!ent) return -ENOENT;
    }
    
    struct myhandle *h = (struct myhandle*)malloc(sizeof(*h));
    
    h->tmpbuf = (char*)malloc(block_size);
    h->current_block = -1;
    h->ent = ent;
    fi->fh = (intptr_t)h;
    h->is_dirty = 0;
    
    return 0;
}

static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	return xmp_open(path, fi);
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
    struct myhandle* h = (struct myhandle*)(intptr_t)fi->fh;
    struct mydirent* ent = h->ent;
    
    if(offset > ent->length) return 0;
    
    if (size + offset > ent->length) size=ent->length - offset;
        
    if (size<=0) return 0;
        
    int buf_offset = 0;
        
    int saved_size = size;
    
    while (size>0) {
        int block_number = (offset / block_size);
        
        if(h->current_block != block_number) {
            if (h->is_dirty) {
                int ret = write_block((unsigned char*)h->tmpbuf, &ent->blocks[h->current_block]);
                if (!ret) readonly_flag=1;
                h->is_dirty = 0;
            }
            read_block((unsigned char*)h->tmpbuf, &ent->blocks[block_number]);
            h->current_block = block_number;
        }
        
        int minioffset = offset - block_size*block_number;
        int minilen = block_size-minioffset;
        if (size < minilen) minilen = size;
            
        memcpy(buf+buf_offset, h->tmpbuf + minioffset, minilen);
        
        buf_offset += minilen;
        size-=minilen;
        offset+=minilen;
    }
    
	return saved_size;
}

static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
    if(readonly_flag) return -EROFS;
    struct myhandle* h = (struct myhandle*)(intptr_t)fi->fh;
    struct mydirent* ent = h->ent;
    
    int ret = ensure_size(ent, offset+size);
    if(!ret) return -ENOSPC;
        
    int buf_offset = 0;
    
    size_t saved_size = size;
    
    while (size>0) {
        int block_number = (offset / block_size);
        
        if(h->current_block != block_number) {
            if (block_number >= ent->blocks_array_size) {
                return -EINVAL;
            }
            if (h->is_dirty) {
                int ret = write_block((unsigned char*)h->tmpbuf, &ent->blocks[h->current_block]);
                if (!ret) readonly_flag=1;
                h->is_dirty = 0;
                if (!ret) return -EINVAL;
            }
            read_block((unsigned char*)h->tmpbuf, &ent->blocks[block_number]);
            h->current_block = block_number;
        }
        
        int minioffset = offset - block_size*block_number;
        int minilen = block_size-minioffset;
        if (size < minilen) minilen = size;
            
        memcpy(h->tmpbuf + minioffset, buf+buf_offset, minilen);
        h->is_dirty=1;
        
        buf_offset += minilen;
        size-=minilen;
        offset+=minilen;
    }
    
    dirty_bytes+=saved_size;
    ++dirty_status;
    
    if (dirty_bytes > max_dirty_bytes || dirty_status > max_dirty_calls) {
        save_entries(user_first_block);
    } else {
        raise_alarm();
    }
	return saved_size;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	return -ENOENT;
}

static int xmp_flush(const char *path, struct fuse_file_info *fi)
{
	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
    struct myhandle* h = (struct myhandle*)(intptr_t)fi->fh;
        
    if (h->is_dirty) {
        int ret = write_block((unsigned char*)h->tmpbuf, &h->ent->blocks[h->current_block]);
        if (!ret) readonly_flag=1;
        h->is_dirty = 0;
    }
    
    free(h->tmpbuf);
    free(h);
    
    if (dirty_bytes>0) {
        save_entries(user_first_block);
    }
	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	return 0;
}


static void xmp_destroy(void* unused)
{
    save_entries(user_first_block);
}


static struct fuse_operations xmp_oper = {
	.getattr	= xmp_getattr,
	.access		= xmp_access,
	.readlink	= xmp_readlink,
	.readdir	= xmp_readdir,
	.mkdir		= xmp_mkdir,
	.unlink		= xmp_unlink,
	.rmdir		= xmp_rmdir,
	.rename		= xmp_rename,
	.chmod		= xmp_chmod,
	.chown		= xmp_chown,
	.truncate	= xmp_truncate,
	.utimens	= xmp_utimens,
	.create		= xmp_create,
	.open		= xmp_open,
	.read		= xmp_read,
	.write		= xmp_write,
	.statfs		= xmp_statfs,
	.flush		= xmp_flush,
	.release	= xmp_release,
	.fsync		= xmp_fsync,
    .destroy    = xmp_destroy,
};

void sigalm() {
    //save_entries(user_first_block);
    alarm_triggered=0;
}

char passwords_area[65536];

int main(int argc, char* argv[]) {
    block_size = 8192;
    rnd_name = "/dev/urandom";
    max_dirty_bytes = 1000000;
    max_dirty_calls = 1000;
    no_shred = 0;
    no_sync = 0;
    dirty_alarm_timeout=5;
    reserved_percent=5;
    int no_o_direct = 0;
    
    
    if (argc < 3) {
        fprintf(stderr, "Usage: chaoticfs data_file mountpoint [FUSE options]\n");
        fprintf(stderr, "Environment variables:\n");
        fprintf(stderr, "   BLOCK_SIZE, default %d\n", block_size);
        fprintf(stderr, "   RANDOM_FILE, default %s\n", rnd_name);
        fprintf(stderr, "   MAX_DIRTY_BYTES, default %d\n", max_dirty_bytes);
        fprintf(stderr, "   MAX_DIRTY_CALLS, default %d\n", max_dirty_calls);
        fprintf(stderr, "   NO_SHRED\n");
        fprintf(stderr, "   NO_SYNC\n");
        fprintf(stderr, "   NO_O_DIRECT\n");
        fprintf(stderr, "   RESERVED_PERCENT, default %d\n", reserved_percent);
        fprintf(stderr, "   RANDOM_SHRED_PROBABILITY %d of 1000\n", random_shred_probability);
        fprintf(stderr, "\n");
        fprintf(stderr, "   MCRYPT_ALGO, default %s\n", mcrypt_algo);
        fprintf(stderr, "   MCRYPT_MODE, default %s\n", mcrypt_mode);
        fprintf(stderr, "   MCRYPT_KEYSIZE, default %d\n", mcrypt_keysize);
        fprintf(stderr, "   HASH_ALGO, default %d\n", hash_algo);
        fprintf(stderr, "   KEYGEN_ALGO, default %d\n", keygen_algo);
        fprintf(stderr, "   KEYGEN_COUNT, default %d\n", keygen_count);
        fprintf(stderr, "   KEYGEN_SALT, default %s\n", keygen_salt);
        return 1;
    }
    
    if (getenv("NO_O_DIRECT")) no_o_direct=1;
    if (getenv("BLOCK_SIZE")) {
        block_size = atoi(getenv("BLOCK_SIZE"));
        if (!no_o_direct) {
            if (block_size<sysconf(_SC_PAGESIZE)) fprintf(stderr, "I don't like this small BLOCK_SIZE\nUse NO_O_DIRECT.\n");
            if ((block_size & (block_size-1)) != 0) fprintf(stderr, "I don't like this not-power-of-two LOCK_SIZE\nUse NO_O_DIRECT.\n");
        }
    }
    if (getenv("RANDOM_FILE")) rnd_name = getenv("RANDOM_FILE");
    if (getenv("MAX_DIRTY_BYTES")) max_dirty_bytes = atoi(getenv("MAX_DIRTY_BYTES"));
    if (getenv("MAX_DIRTY_CALLS")) max_dirty_calls = atoi(getenv("MAX_DIRTY_CALLS"));
    if (getenv("DIRTY_ALARM")) dirty_alarm_timeout = atoi(getenv("DIRTY_ALARM"));
    if (getenv("NO_SHRED")) no_shred=1;
    if (getenv("NO_SYNC")) no_sync=1;
    if (getenv("RESERVED_PERCENT")) reserved_percent = atoi(getenv("RESERVED_PERCENT"));
    if (getenv("RANDOM_SHRED_PROBABILITY")) random_shred_probability = atoi(getenv("RANDOM_SHRED_PROBABILITY"));
    
    data_name = argv[1];
    user_first_block = 2;
    
    rnd= fopen(rnd_name, "rb");
    if(!rnd) { perror("fopen random"); return 2; }
    data = open(data_name, O_RDWR | (no_o_direct?0:O_DIRECT), 0777);
    if(data<0) { perror("open data"); return 3; }
    
    {
        struct stat st;
        fstat(data,  &st);
        long long int len = st.st_size;
        block_count = (len / block_size);
        if (block_count<1) {
            fprintf(stderr, "Data file is empty. It should be pre-initialized with random data\n");
            return 4;
        }
    }
    
    shred_buffer = (unsigned char*) malloc(block_size);
    busy_map = (unsigned char*) malloc(block_count);
    mcrypt_buf = (unsigned char*) valloc(block_size);
    busy_blocks_count = 0;
    memset(busy_map, 0, block_count);
    saved_directory_blocks_size = 0;
    saved_directory_blocks = NULL;
    alarm_triggered = 0;
    
    {
        if (getenv("MCRYPT_ALGO")) { mcrypt_algo = getenv("MCRYPT_ALGO"); }
        if (getenv("MCRYPT_MODE")) { mcrypt_mode = getenv("MCRYPT_MODE"); }
        if (getenv("MCRYPT_KEYSIZE")) { mcrypt_keysize=atoi(getenv("MCRYPT_KEYSIZE"))/8; }
        if (getenv("HASH_ALGO")) { hash_algo=atoi(getenv("HASH_ALGO")); }
        if (getenv("KEYGEN_ALGO")) { keygen_algo=atoi(getenv("KEYGEN_ALGO")); }
        if (getenv("KEYGEN_COUNT")) { keygen_count=atoi(getenv("KEYGEN_COUNT")); }
        if (getenv("KEYGEN_SALT")) { keygen_salt=getenv("KEYGEN_SALT"); }
        

        if (strcmp(mcrypt_algo, "none") && strcmp(mcrypt_mode, "none")) {

            mcrypt = mcrypt_module_open(mcrypt_algo, NULL, mcrypt_mode, NULL);
            
            if (mcrypt==MCRYPT_FAILED) {
                fprintf(stderr, "mcrypt_module_open failed algo=%s mode=%s keysize=%d\n", mcrypt_algo, mcrypt_mode, mcrypt_keysize);
                return 11;
            }
            mcrypt_blocksize = mcrypt_enc_get_block_size(mcrypt);
            mcrypt_ivsize = mcrypt_enc_get_iv_size(mcrypt);            
        }
    }
    {
        printf("Enter the comma-separated blockpasswords list (example: \"2sK1m49se,5sldmIqaa,853svmqpsd\")\n");
        
        {
            struct termios old, new_;
            int ret = tcgetattr(0, &old);
            if (!ret) {
                memcpy(&new_, &old, sizeof(old));
                new_.c_lflag &= ~ECHO;
                tcsetattr (0, TCSAFLUSH, &new_);
            }
            fgets(passwords_area, sizeof(passwords_area), stdin);
            if (!ret) {
                tcsetattr (0, TCSAFLUSH, &old);
            }
        }
        
        passwords_area[sizeof(passwords_area)-1]=0;
        if (passwords_area[strlen(passwords_area)-1] == '\n') passwords_area[strlen(passwords_area)-1]=0;
        char* s = strtok(passwords_area, ",");
        char* n;
        while(s) {
            user_first_block = atoi(s);
            if (user_first_block<0 || user_first_block>=block_count) {
               fprintf(stderr, "Block number is out of range\n");
               return 39; 
            }
            if (busy_map[user_first_block]) {
                fprintf(stderr, "Duplicate/used block number\n");
                return 40;
            }
            if (mcrypt != MCRYPT_FAILED) {
                if (!mcrypt_key) {
                    mcrypt_key = (char*)malloc(mcrypt_keysize);
                    mlock(mcrypt_key, mcrypt_keysize);
                }
                if (!mcrypt_ivbuf) {
                    mcrypt_ivbuf = (unsigned char*) malloc(mcrypt_ivsize);
                }
                
                KEYGEN kg;
                kg.hash_algorithm[0]=hash_algo;
                kg.hash_algorithm[1]=hash_algo;
                kg.count=keygen_count;
                kg.salt = keygen_salt;
                kg.salt_size = strlen(keygen_salt);
                
                int ret = mhash_keygen_ext(keygen_algo, kg, mcrypt_key, mcrypt_keysize, (unsigned char*)s, strlen(s));
                
                if (ret!=0) {
                    fprintf(stderr, "Failed to generate key for password\n");
                    perror("mhash_keygen_ext");
                    return 42;
                }
                
                /*
                ret = mcrypt_generic_init(mcrypt, mcrypt_key, mcrypt_keysize/8, NULL);
                
                if (ret) {
                    mcrypt_perror(ret);
                    return 44;
                }*/
                
            }
            n = strtok(NULL, ",");
            if (n) {
                mark_used_block(user_first_block);
                int r = load_entries(user_first_block, 1);
                if (!r) {
                    fprintf(stderr, "No entries loaded for auxilary branch, maybe need better password\n");
                    return 43;
                }
                //mcrypt_generic_deinit(mcrypt);
            }
            s=n;
        }    
    }
    mlock(busy_map, block_count);
    mark_used_block(user_first_block);
    
    current_dirent_array_size = 128;
    dirents = (struct mydirent*) malloc(current_dirent_array_size * sizeof(*dirents));
    dirent_entries_count=0;
    
    readonly_flag = 0;
    dirty_status = 0;
    
    {
        struct sigaction sa = {{&sigalm}};
        sigaction(SIGALRM, &sa, NULL);
    }
    
    int ret;
    
    if (!strcmp(argv[2], "--debug-generate")) {
        generate_test_dirents();
    }
    else if (!strcmp(argv[2], "--debug-print")) {
        debug_print_dirents(user_first_block);        
    } else {
        int r = load_entries(user_first_block, 0);
        
        if (!r) {
            fprintf(stderr, "No entries loaded, creating default entry\n");
            create_dirent("/");
        } else {
            printf("Directory loaded successfully\n");
        }
        
        
        #define MY 2
        char** new_argv = (char**)malloc( (argc-1+MY+1) * sizeof(char*));
        new_argv[0]="chaoticfs";
        // "My" args
        new_argv[1]="-s"; // single threaded
        new_argv[2]="-osubtype=chaoticfs";
        int i;
        for(i=2; i<argc; ++i) {
            new_argv[i-1+MY] = argv[i];
        }
        new_argv[i-1+MY]=NULL;
        ret = fuse_main(i-1+MY, new_argv, &xmp_oper, NULL);
        free(new_argv);
    }
    
    
    free(dirents);
    free(busy_map);
    close(data);
    fclose(rnd);
    return ret;
}
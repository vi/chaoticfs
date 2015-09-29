chaoticfs is special encrypting 
FUSE-based filesystem that allows you to 
[partially expose](http://en.wikipedia.org/wiki/Plausible_deniability#Use_in_cryptography) 
your data by using multiple entry points.
The filesystem is geared towards simplicity and
not stores any "extra" things like timestamps or
sequence numbers which can make the task of
reliably hiding the presence of additional data
complexier.


Features
===
* Random block allocation everywhere except of the first
directory block that is the part of password, so no
statistical issues in the "free space" when partial
data is exposed;
* No (access, modification, creation) dates and times;
* No unencrypted signatures. The storage is a plain
file that looks like just a block of random data;
* Easy extending: just append random data to storage
(note: extension event is noticeable in statistics
of free blocks).
* Deleted files gets shredded (simple single
overwrite by random data) by default;
* Directories are stored as the whole thing 
each time, no "holes" for deleted files in directories;
* No "100% exposure" possible by design.
There should be about 5% (by default) of some reserved 
unused random data which _maybe_ holds additional branchs.
There are additional random writes to unused blocks 
from time to time by default.
* Blocks saving is randomized using 32-bit IVs. Each block write operation uses a new IV. Default cipher mode is nOFB to make IV affect the whole block.
* Directory information is saved automatically from time to time. You should unmount chaoticfs properly to be sure about your data although, there's (currently) no "dirty_writeback_centisecs", only "dirty_bytes"...

Misfeatures
===
* 

        Hacky unrefactored C cod
        Segmentation fault
        
* O(n) in many places, no indexes;
* The whole metadata (filenames, block lists) 
is kept in memory and serialized to storage at once
* No fsck/recovery utility (yet)
* No symlinks, attributes, sparse files 
and other advanced filesystem features
* Not designed to be fast
* Not designed to be reliable
* Not designed to hold many files or big files
* No tool to access the filesystem when
FUSE is not available (yet)

Usage
===

Basic commands
---
1. Generate some random file

        $ dd if=/dev/urandom bs=1M count=100 of=data.rnd
        
2. Choose the blockpassword and mount the file

        $ ./chaoticfs data.rnd  m
        Enter the comma-separated blockpasswords list (example: "2sK1m49se,5sldmIqaa,853svmqpsd")
        33password
        No entries loaded, creating default entry
        
    Blockpassword is a password with embedded block number (the leading numeric part of the password). chaoticfs can reject a blockpassword in case of busy block.
        
3. Create something to make chaoticfs save the directory

        $ mkdir m/test
        $ fusermount -u m
        
4. Create another branch

        ./chaoticfs data.rnd  m
        Enter the comma-separated blockpasswords list (example: "2sK1m49se,5sldmIqaa,853svmqpsd")
        33password,42secret
        No entries loaded, creating default entry
        echo qqq > m/qqq
        fusermount -u m
        
    The last specified blockpassword is the visible branch.
    All other blockpasswords are only for marking which blocks should be preserved.
        
Messages:
---
* `No entries loaded, creating default entry` - can't load 
the directory from the last blockpassword.
If you are creating a branch, then continue.
If it is because of incorrect password, then unmount and retry (otherwise you can overwrite the correct entry).

* `No entries loaded for auxiliary branch, maybe need better password` - 
can't load the directory for non-last blockpassword.
chaoticfs aborts in this case.

* `Duplicate/used block number` - trying to point the new blockpassword to already used block. Just choose some other block number (the leading numeric part of the password). Should not happen when not trying to create a branch. chaoticfs aborts in this case.

* `Block number is out of range` - Block number from your password is too big. Choose smaller or extend the storage file.

* `Directory loaded successfully` - Successfully read all directories.

Concerns
---
* Don't rely on safety of the storage.
Use SHA1SUMS, unmount, remount and check SHA1SUMS to be sure.
You can backup chaoticfs storage (~~comparing backups
should not releal too much info~~ /\* TODO \*/).
* Other things can point to the "missing" secret parts
of the directory.
* Wrong password + unnoticed message or empiness of the directory => defuct branch. Forget to specify some branch => spoiled data in that branch.
* Just the usage of chaoticfs (or similar 
"false bottom"-centered things) means you
are hiding something. You will look silly if you disclose
only one branch...  
    Use 3-10 branches and keep the actual number
    of branches secret. 
    Combined with the previous point it means that
    secure usage of this version of chaoticfs is
    a bit inconvenient.
    
Filesystem format
===

Block level
---
The filesystem is made of fixed size blocks
(BLOCK_SIZE, by default 8192).
Each block can be free or contain [part of] directory or file content.
"Superblock" is just the first directory's block.
Each block is numbered from 0 to (fize_size/block_size-1).

Each block is encrypted (by default rijndael-256
 in nOFB mode) with a key and
 32-bit initialization vector. 
 Trailing bytes of the IV buffer are 
 initialized with zeroes. 
 
For file's content, IV is generated 
 randomly each time the block is written.
 The IV is stored in the directory.

For directory's content, IV is big-endian 
 32-bit block number; but directory blocks get
 addititionally "scrambled" by XORing the first
 4 bytes longint over all remaining longints.
 ~~This is to make updated directory content
 appear to be completely random when comparing backups
 of chaoticfs storage.~~
 (I'm calling this "Poor man's IV")
 
    
Directory
---
There is only one directory per branch which 
stores all files and directories in the branch,
identified by the full path. Each entry of the
directory is called direntry
("mydirent" structure in the code)

Directory is represented by empty file 
with the path ending in "/".

File's path must not end in "/".

There always should be a "/" direntry.

Each direntry have path, file length and block list fields.
Block list is the list of block numbers together with
IVs for decryption.

Serialized directory
---

The directory is saved to dirent blocks.
The first dirent block number is stored in the
password itself.

Each dirent block has a header and one or more entries.

Header is 8 random bytes, then 8-byte signature "RndAllV0".

Each entry represents a direntry.
If direntry's block list can't fit in the block,
another "duplicate" dirent gets created
with nonzero block index offset.

Each entry consists of:

* direntry path's string length - 4 bytes, big endian;
* direntry path - variable number of bytes;
* file length - 8 bytes, big endian;
* number of blocks in this entry - 4 bytes, big endian;
* block index offset in this entry - 4 bytes, big endian;
* block idexes and IVs - zero of more bytes
    * a block index - 4 bypes, big endian;
    * the IV for this block - 4 bytes.
* block number for the next entry - 4 bytes, big endian;
* offset in the block for the next entry -
            4 bytes, big endian;
* reserved - 8 bytes.
            
If the block number and offset both equal to zero then this
is the last direntry.

Example:

    #block 2:
    \x34\xF5\x12\x46\x79\x12\x15\x05  # random bytes
    RndAllV0 # signature
    
    \x00\x00\x00\x01 # name length
    / # name
    \x00\x00\x00\x00\x00\x00\x00\x00 # file length
    \x00\x00\x00\x00 # no blocks at all for directories
    \x00\x00\x00\x00 # no blocks at all for directories
    # no blocks
    \x00\x00\x00\x02 # the next entry is also in block 2
    \x00\x00\x00\x35 # the next direntry is at offset 53
    \x00\x00\x00\x00\x00\x00\x00\x00 - reserved
    
    \x00\x00\x00\x06 # name length
    /qwer/ # name
    \x00\x00\x00\x00\x00\x00\x00\x00 # file length
    \x00\x00\x00\x00 # no blocks at all for directories
    \x00\x00\x00\x00 # no blocks at all for directories
    # no blocks
    \x00\x00\x00\x02 # the next entry is also in block 2
    \x00\x00\x00\x5F # the next direntry is at offset 95
    \x00\x00\x00\x00\x00\x00\x00\x00 reserved
    
    \x00\x00\x00\x10 # name length
    /qwer/secret.txt # name
    \x00\x00\x00\x00\x00\x00\xE0\x00 # file length
    \x00\x00\x00\x05 # only pointing to 5 first blocks here
    \x00\x00\x00\x00 # first block is file's 0'th block
    \x00\x00\x23\x5C # file's block 0 is has index 9015
    \x49\xF4\xF9\xCC # file's block 0 use IV 49F9F9CC
    \x00\x00\x20\xBF # file's block 1 is has index 9015
    \x44\x56\x23\xC2 # file's block 1 use IV 445623C2
    \x00\x00\x12\x05 # file's block 2 is has index 4613
    \x23\xCD\x98\xD3 # file's block 2 use IV 23CD98D3
    \x00\x00\x03\x45 # file's block 3 is has index 837
    \x6F\xD4\x23\xB6 # file's block 3 use IV 6FD423B6
    \x00\x00\x04\x23 # file's block 4 is has index 1059
    \x23\x5D\xF8\x6F # file's block 4 use IV 235DF86F
    \x00\x00\x0F\xDC # the next entry is in block 4045
    \x00\x00\x00\x10 # the next direntry is at offset 16
    \x00\x00\x00\x00\x00\x00\x00\x00 reserved
    
    --
    #block 4045:
    \x98\xCF\xDE\xBF\x93\x23\xF3\xFF  # random bytes
    RndAllV0 # signature
    \x00\x00\x00\x00\x00\x00\xE0\x00 # file length
    \x00\x00\x00\x02 # pointing to 2 remaining blocks here
    \x00\x00\x00\x05 # first block here is file's 5'th block
    \x00\x00\x37\x3D # file's block 5 is has index 14301
    \x8C\xCC\x32\xDD # file's block 5 use IV 8CCC32DD
    \x00\x00\x34\x12 # file's block 6 is has index 13330
    \xD3\x97\x57\xC7 # file's block 6 use IV D49757C7
    \x00\x00\x00\x00 # no next entry
    \x00\x00\x00\x00 # no next entry
    \x00\x00\x00\x00\x00\x00\x00\x00 reserved
    
    


Use `--debug-print` to see the dump the directory.

Todo
===
1. At least minimal refactor (split to multiple source files, isolate layers)
2. Implement non-FUSE-based tool to access chaoricfs
3. FTP interface to chaoticfs (to use in Windows)?
4. Fsck/recovery tool? 
5. Change filesystem format for things to be O(log n), proper sudden shutdown behaviour, etc. to make it "chaotic good" system.
6. Access to multiple branches using parts 
    of path to discriminate.
    Storing of passwords file in "master" branch
    to avoid typing lengthy password list for
    all branchs every time on mount.

For just a normal encrypting FUSE filesystem see [EncFS](http://www.arg0.net/encfs).

Create issues on GitHub's tracker if you want
something to be improved.
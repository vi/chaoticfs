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
* No symlinks and other advanced filesystem features
* Not designed to be fast
* Not designed to be reliable
* Not designed to hold many files or big files
* No tool to access the filesystem when FUSE is not available (yet)

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

* `No entries loaded for auxilary branch, maybe need better password` - 
can't load the directory for non-last blockpassword.
chaoticfs aborts in this case.

* `Duplicate/used block number` - trying to point the new blockpassword to already used block. Just choose some other block number (the leading numeric part of the password). Should not happen when not trying to create a branch. chaoticfs aborts in this case.

* `Block number is out of range` - Block number from your password is too big. Choose smaller or extend the storage file.

* `Directory loaded successfully` - Successfully read all directories.

Concerns
---
* Don't rely on safety of the storage.
Use SHA1SUMS, unmount, remount and check SHA1SUMS to be sure.
You can backup chaoticfs storage (comparing backups
should not releal too much info).
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
randomallocfs is special encrypting FUSE-based filesystem that allows you to [partially expose](http://en.wikipedia.org/wiki/Plausible_deniability#Use_in_cryptography) your data by using multiple entry points.
The filesystem is geared towards simplicity and not stores any "extra" things like timestamps or sequence numbers which can make the task of reliably hiding the presence of additional data complexier.


Features
----
* Random block allocation everywhere except of the first directory block that is the part of password, so no statistical issues in the "free space" when partial data is exposed;
* No (access, modification, creation) dates and times by design;
* No unencrypted signatures. The storage is a plain file that looks like just a block of random data;
* Easy extending: just append random data to storage (note: extension is noticeable in statistics of free blocks).
* Deleted files gets shredded (simple single overwrite by random data) by default;
* Directories are stored as the whole thing each time, no "holes" for deleted files in directories;
* No "100% exposure" possible by design. There should be about 5% (by default) of some reserved unused random data which _maybe_ holds additional files.

Misfeatures
---
* Hacky unrefactored C code;

        Segmentation fault
        
* O(n) almost everywhere, no indexes;
* The whole metadata (filenames, block lists) is kept in memory and serialized to storage at once
* No fsck/recovery utility (yet)
* No symlinks and other advanced filesystem features
* Not designed to be fast
* Not designed to be reliable
* Not designed to hold many files or big files
* No tool to access the filesystem when FUSE is not available (yet)

For just a normal encrypting FUSE filesystem see [EncFS](http://www.arg0.net/encfs).

Create issues on GitHub's tracker if you want something to be improved.
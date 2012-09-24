randomallocfs: *.c
	    gcc -ggdb -Wall `pkg-config fuse --cflags --libs` -lmcrypt -lmhash randomallocfs.c -o randomallocfs
		
test: randomallocfs
		./test.sh
    
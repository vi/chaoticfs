randomallocfs: *.c
	    gcc -ggdb -Wall `pkg-config fuse --cflags --libs` -lmcrypt randomallocfs.c -o randomallocfs
		
test: randomallocfs
		./test.sh
    
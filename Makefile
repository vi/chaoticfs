randomallocfs: *.c
	    gcc -ggdb -Wall `pkg-config fuse --cflags --libs` randomallocfs.c -o randomallocfs
		
test: randomallocfs
		./test.sh
    
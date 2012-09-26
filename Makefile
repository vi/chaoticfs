chaoticfs: *.c
	    gcc -ggdb -Wall `pkg-config fuse --cflags --libs` -lmcrypt -lmhash randomallocfs.c -o chaoticfs
		
test: chaoticfs
		./test.sh
    

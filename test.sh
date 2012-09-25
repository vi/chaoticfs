#!/bin/bash

set -e
set -E

trap 'echo "Test failed"' ERR

function setup() {
    fusermount -u m 2> /dev/null || true
    mkdir -p m
    dd if=/dev/zero of=s bs=1024 count=1024 2> /dev/null
    echo "2test" | ./randomallocfs s m  > /dev/null 2> /dev/null
}

function teardown() {
    fusermount -u m 2> /dev/null || true
    rm -f s
    rmdir m
}

function chopfile() {
    SIZE="$1"
    KSIZE=$((SIZE/1024))
    BSIZE=$((SIZE%1024))
    
    dd bs=1024 count=$KSIZE 2> /dev/null
    dd bs=1 count=$BSIZE 2> /dev/null
}

function measuretransferred() {
    perl -e '
        $SIGNAL{"PIPE"}="IGNORE"; 
        $q=0; while() {
            $r=sysread(STDIN, $b, 1024); 
            last unless $r>0; 
            $r2=syswrite(STDOUT, $b, $r); 
            last unless $r2; $q+=$r2;
        } 
        print STDERR "$q\n"
    '
}

function tests() {

echo "BLOCK_SIZE=$BLOCK_SIZE"

echo "Dummy test"
setup
diff -u - <(find m -printf '%y %M %n %s %P %Cs %As %Ts\n') <<\EOF
d drwxr-x--- 0 0  0 0 0
EOF
teardown

echo "Simple storing test"
setup
echo qqq > m/qqq
mkdir m/ddd
diff -u - <(find m -printf '%y %M %n %s %P %Cs %As %Ts\n') <<\EOF
d drwxr-x--- 0 0  0 0 0
f -rwxr-x--- 0 4 qqq 0 0 0
d drwxr-x--- 0 0 ddd 0 0 0
EOF
teardown

echo "Simple persistence test"
setup
echo qqq > m/qqq
mkdir m/ddd
fusermount -u m
echo "2test" | ./randomallocfs s m > /dev/null
diff -u - <(find m -printf '%y %M %n %s %P %Cs %As %Ts\n') <<\EOF
d drwxr-x--- 0 0  0 0 0
f -rwxr-x--- 0 4 qqq 0 0 0
d drwxr-x--- 0 0 ddd 0 0 0
EOF
teardown

echo "ENOSPC test"
setup
yes ABCDEFGH | nl | cat > m/file || true
SIZE=`find m/file -printf '%s'`
echo "  Uses space: " $((SIZE*100/1024/1024)) "%"
fusermount -u m
sleep 1 # XXX
echo "2test" | ./randomallocfs s m > /dev/null
SIZE2=`find m/file -printf '%s'`
test "$SIZE" == "$SIZE2"
diff -u <(yes ABCDEFGH | nl | chopfile "$SIZE") m/file
teardown


echo "Sudden shutdown test"
dd if=/dev/zero of=s bs=1024 count=102400 2> /dev/null
mkdir -p m
echo "2test" | ./randomallocfs s m > /dev/null 2> /dev/null

dd if=/dev/urandom bs=1024 count=128 of=randomlet 2> /dev/null
rm -f testfile
for((i=0; i<500; ++i)) { echo $i >> testfile; cat randomlet >> testfile; }
rm randomlet;

cat testfile | measuretransferred > m/file 2> stats&
sleep 20 # adjust this it your computer is too fast
pkill -9 -f 'randomallocfs s m'
sleep 1
fusermount -u m || { sleep 2 && fusermount -u m; } || { sleep 10 && fusermount -u m; }
echo "2test" | ./randomallocfs s m > /dev/null
SIZE=$(<stats)
if [ ! -e m/file ]; then
    echo "    All $SIZE bytes lost";
else
    SIZE2=`find m/file -printf '%s'`
    echo "    Lost bytes: " $((SIZE-SIZE2)) " of $SIZE"
    if [ "$SIZE2" -lt "$SIZE" ]; then MINSIZE=$SIZE2; else MINSIZE=$SIZE; fi
    MINSIZE=$((MINSIZE-BLOCK_SIZE)) # allow last block to be invalid
    cmp -n "$MINSIZE" testfile m/file
fi
rm testfile
rm stats
teardown

}


BLOCK_SIZE=128 NO_O_DIRECT=y tests
BLOCK_SIZE=1024 NO_O_DIRECT=y tests
BLOCK_SIZE=4096 tests
BLOCK_SIZE=65536 tests


export BLOCK_SIZE=8192

echo "Filesystem name test"
setup
echo qqq > m/qqq
mkdir m/ddd
diff -u - <(find m -printf '%y %M %n %s %P %F %Cs %As %Ts\n') <<\EOF
d drwxr-x--- 0 0  fuse.randomallocfs 0 0 0
f -rwxr-x--- 0 4 qqq fuse.randomallocfs 0 0 0
d drwxr-x--- 0 0 ddd fuse.randomallocfs 0 0 0
EOF
teardown

echo "All tests finished."

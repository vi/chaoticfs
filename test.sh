#!/bin/bash

set -e
set -E

trap 'echo "Test failed"' ERR

function setup() {
    fusermount -u m 2> /dev/null || true
    mkdir -p m
    dd if=/dev/zero of=s bs=1024 count=1024 2> /dev/null
    echo "2test" | ./chaoticfs s m  > /dev/null 2> /dev/null
}

function um() {
    fusermount -u m || { sleep 2 && fusermount -u m; } ||
    while true; do
        fusermount -u m && break || sleep 10;
    done
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
echo "2test" | ./chaoticfs s m > /dev/null
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
um
sleep 1 # XXX
echo "2test" | ./chaoticfs s m > /dev/null
SIZE2=`find m/file -printf '%s'`
test "$SIZE" == "$SIZE2"
diff -u <(yes ABCDEFGH | nl | chopfile "$SIZE") m/file
teardown

echo "Complex test"
echo "   prepare data"
dd if=/dev/zero of=s bs=1024 count=102400 2> /dev/null
rm -Rf dataset1 dataset2 dataset3
mkdir -p dataset1 dataset2 dataset3
dd if=/dev/urandom bs=1337 count=128 of=randomlet 2> /dev/null
for i in {1..10}; do
    for j in `seq 1 $i`; do
        echo "ds1 $j" >> dataset1/$i.dat
        cat randomlet >> dataset1/$i.dat
        echo "2 $j" >> dataset2/$i.dat
        cat randomlet >> dataset2/$i.dat
        echo "3333333333333333333333333333333 $j" >> dataset3/$i.dat
        cat randomlet >> dataset3/$i.dat
    done
done
(cd dataset1 && sha1sum *.dat > SHA1SUMS;)
(cd dataset2 && sha1sum *.dat > SHA1SUMS;)
(cd dataset3 && sha1sum *.dat > SHA1SUMS;)

echo "   copy data"
mkdir -p m
echo "2test" | ./chaoticfs s m > /dev/null 2> /dev/null
cp -R dataset1 m/
um

mkdir -p m
echo "2test,3test" | ./chaoticfs s m > /dev/null 2> /dev/null
cp -R dataset2 m/
um

mkdir -p m
echo "2test,3test" | ./chaoticfs s m > /dev/null 2> /dev/null
cp -R dataset2 m/
um

mkdir -p m
echo "2test,3test,4test" | ./chaoticfs s m > /dev/null 2> /dev/null
cp -R dataset3 m/
um

mkdir -p m
echo "4test,2test,3test" | ./chaoticfs s m > /dev/null 2> /dev/null
cp -R dataset2 m/
um

echo "   check data"
echo "2test" | ./chaoticfs s m > /dev/null 2> /dev/null
cmp dataset1/SHA1SUMS m/dataset1/SHA1SUMS
(cd m/dataset1/ && sha1sum --quiet -c SHA1SUMS)
um

echo "3test" | ./chaoticfs s m > /dev/null 2> /dev/null
cmp dataset2/SHA1SUMS m/dataset2/SHA1SUMS
(cd m/dataset2/ && sha1sum --quiet -c SHA1SUMS)
um

echo "4test" | ./chaoticfs s m > /dev/null 2> /dev/null
cmp dataset3/SHA1SUMS m/dataset3/SHA1SUMS
(cd m/dataset3/ && sha1sum --quiet -c SHA1SUMS)
um

rm -Rf dataset1 dataset2 dataset3 m


echo "Sudden shutdown test"
dd if=/dev/zero of=s bs=1024 count=102400 2> /dev/null
mkdir -p m
echo "2test" | ./chaoticfs s m > /dev/null 2> /dev/null

dd if=/dev/urandom bs=1024 count=128 of=randomlet 2> /dev/null
rm -f testfile
for((i=0; i<500; ++i)) { echo $i >> testfile; cat randomlet >> testfile; }
rm randomlet;

cat testfile | measuretransferred > m/file 2> stats&
sleep 20 # adjust this it your computer is too fast
pkill -9 -f 'chaoticfs s m'
sleep 1
um
echo "2test" | ./chaoticfs s m > /dev/null
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

BLOCK_SIZE=8192 tests
BLOCK_SIZE=4096 tests
BLOCK_SIZE=128 NO_O_DIRECT=y tests
BLOCK_SIZE=1024 NO_O_DIRECT=y tests
BLOCK_SIZE=65536 tests


export BLOCK_SIZE=8192

echo "Filesystem name test"
setup
echo qqq > m/qqq
mkdir m/ddd
diff -u - <(find m -printf '%y %M %n %s %P %F %Cs %As %Ts\n') <<\EOF
d drwxr-x--- 0 0  fuse.chaoticfs 0 0 0
f -rwxr-x--- 0 4 qqq fuse.chaoticfs 0 0 0
d drwxr-x--- 0 0 ddd fuse.chaoticfs 0 0 0
EOF
teardown

echo "All tests finished."

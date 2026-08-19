#!/bin/sh

DEFAULT_HOSTS="\
vf13dell.dynamic.ablesystem.co.jp \
vf13tx1320-1 \
vf13tx2 \
vf12-1 \
vf13t440-1 \
vf13t440-2 \
vf13t440-3 \
vf14ml150-1 \
vf14ml150-2 \
vf14ml150-3 \
vf14ml150-4 \
vf13tx100 \
vf14windell.dynamic.ablesystem.co.jp \
vf14mini.dynamic.ablesystem.co.jp \
"

# sh scripts/build.sh

if [ "$#" -gt 0 ]; then
    HOSTS="$*"
else
    HOSTS="$DEFAULT_HOSTS"
fi

for HOST in $HOSTS; do
    echo -n "${HOST}:"
    scp -p build/aacgain/aacgain "${HOST}:Bin"
done

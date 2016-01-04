#!/bin/bash

if [ ! -d bin/ ]; then
    echo "Directory 'bin' has not been found, please, build tool using 'make'"
    exit 1
fi

if [ ! -f bin/kiv-zos-fatdefrag ]; then
    echo "Binary file 'kiv-zos-fatdefrag' has not been found, please, build tool using 'make'"
    exit 1
fi

cd bin

for INPUT_FILE in output.fout.fat origbigfat.fat; do
    for THREAD_COUNT in 1 4 8 16; do
        echo Using $THREAD_COUNT threads on file $INPUT_FILE
        time ./kiv-zos-fatdefrag -md -i $INPUT_FILE -t $THREAD_COUNT > /dev/null
    done;
done;

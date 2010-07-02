#!/bin/bash


start()
{
    insmod ./lktrace_fs.ko
    mount -t lktracefs none ./test
}

stop()
{
    umount ./test && rmmod lktrace_fs;
}

if [ $(id -u) != "0" ]
then
    echo -e "$0 must be started as root";
    exit 1
fi

if [ $# -eq 1 ]
then
    eval $1
fi

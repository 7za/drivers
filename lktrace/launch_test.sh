#!/bin/bash

mount_debugfs()
{
	mount -t debugfs none /sys/kernel/debug
}

unmount_debugfs()
{
	umount debugfs
}

start()
{
	mount_debugfs
	insmod ./lktrace_fs.ko
	mount -t lktracefs none ./test
}

stop()
{
	umount ./test && rmmod lktrace_fs;
	unmount_debugfs
}

restart()
{
	stop;
	start;
}

if [ $(id -u) != "0" ]
then
    echo -e "$0 must be started as root";
    exit 1
fi

if [ ! -d "./test" ]
then
	mkdir test
fi

if [ $# -eq 1 ]
then
    eval $1
fi

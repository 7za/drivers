#!/bin/bash

sudo modprobe usbmon
sudo mount -t debugfs none_debugfs /sys/kernel/debug

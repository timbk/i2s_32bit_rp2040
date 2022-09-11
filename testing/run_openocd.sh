#!/bin/sh

~/git/software/openocd-raspberry/src/openocd --search "/home/tim/git/software/openocd-raspberry/tcl/" -f interface/picoprobe.cfg -f target/rp2040.cfg -c "targets rp2040.core0;"

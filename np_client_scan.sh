#!/bin/bash

taskset -c 1 ./NPtcp_scan -h 192.168.1.$1 -l $2 -u $3 -p 0 -r -I

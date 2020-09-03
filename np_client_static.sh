#!/bin/bash

taskset -c 1 ./NPtcp_static -h 192.168.1.$1 -l $2 -u $2 -n $3 -p 0 -r -I

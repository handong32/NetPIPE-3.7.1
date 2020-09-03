#!/bin/bash

taskset -c 1 ./NPtcp_static -l $1 -u $1 -n $2 -p 0 -r -I

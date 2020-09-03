#!/bin/bash

taskset -c 1 ./NPtcp_scan -l $1 -u $2 -p 0 -r -I

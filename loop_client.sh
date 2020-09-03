#!/bin/bash
export LOOP=${LOOP:='100'}
export MSG=${MSG:='1024'}
export ITER=${ITER:='1000'}

for i in $(seq 1 1 $LOOP)
do
	echo taskset -c 1 ./NPtcp_static -h 192.168.1.$1 -l $MSG -u $MSG -n $ITER -p 0 -r -I
done

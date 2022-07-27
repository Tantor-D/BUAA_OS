#!/bin/bash
gcc -Wall $1 -o test 2> warning.txt
#grep -n "warning" warning.txt | sed "s/warning: //g" warning.txt  > result.txt
grep "warning" warning.txt | sed "s/warning: //g" > result.txt

i=1
gcc $1 -o test 
if [ $? -eq 0 ]
then
	#echo success compile
	while [ $i -le $2 ]
	do
		touch temp
		echo $i > temp
		./test < temp 1>>result.txt
		i=`expr $i + 1`
		rm temp
	done
fi 

pwd >> result.txt

#!/bin/bash

if [ $# -eq 0 ]; then
	echo "usage: $(basename $0) id"
	exit 1
fi
dir=$1
if ((${dir} < 1 || ${dir} > 10)); then
	echo "id client deve essere nell'intervallo [1,10]"
	exit 1
fi

client=./bin/client
sock=LSOfilestorage.sk
path="$( pwd -P )"

#avvio client
		
while true
do
	${client} -f ${sock} -w file/test3/${dir} -D espulsiT3w"$dir" -W file/linux.png,file/testi/prova.txt -D espulsiT3W"$dir" -c ${path}/file/linux.png -l ${path}/file/test3/${dir}/file1.dat -r ${path}/file/test3/${dir}/file1.dat -d lettiT3r"$dir" -u ${path}/file/test3/${dir}/file1.dat 	
		
done

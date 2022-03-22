#!/bin/bash

server=./bin/server

#avvio server in background, salvando il pid 
${server} -f test/configt3.txt &
pid=$!
echo "Server avviato ${pid}"

sleep 2

end=$((SECONDS+30))
i=1

client=./bin/client
sock=LSOfilestorage.sk

while [ $SECONDS -le $end ]; do
	
	${client} -f ${sock} -W file/test3/"$i"/file1.dat -D espulsiT3w"$i" -l "$PWD"/file/test3/"$i"/file1.dat -R -r "$PWD"/file/test3/"$i"/file1.dat -d lettiT3r"$i" -c "$PWD"/file/test3/"$i"/file1.dat
	if [ $((i%10)) = 1 ]; then
		i=1
		sleep 1
	fi
	
done

kill -s SIGINT $pid

wait $pid

exit 0

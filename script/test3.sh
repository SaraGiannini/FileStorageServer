#!/bin/bash

server=./bin/server

#avvio server in background, salvando il pid 
${server} -f test/configt3.txt >test/serverout3.txt &
pid=$!
echo "Server avviato PID:${pid} - Attendere 30 secondi per la sua terminazione"
sleep 30 && kill -INT ${pid} &

chmod +x script/client3.sh

for((i=1;i<=10;i++)); do
	./script/client3.sh ${i} &
	pidcli[i]=$!
done

wait ${pid}

for((i=1;i<=10;i++)); do
	kill -9 ${pidcli[i]}
	wait ${pidcli[i]} 2>/dev/null
done

exit 0

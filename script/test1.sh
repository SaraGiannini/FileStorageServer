#!/bin/bash

server=./bin/server

#avvio server in background, salvando il pid 
valgrind --leak-check=full ${server} -f test/configt1.txt &
pid=$!
echo "Server avviato PID:${pid} - con utilizzo di valgrind"

sleep 1

client=.//bin/client
sock=LSOfilestorage.sk
path="$( pwd -P )" 

#avvio diversi client per testare le operazioni
echo "Avvio 3 Client"

${client} -f ${sock} -p -t 200 -w file/testi -D espulsioniT1\
	-R 3 -d lettiT1R
	

${client} -f ${sock} -p -t 200 -W file/linux.png,file/gdbvalgrind.pdf -D espulsioniT1\
	-l ${path}/file/testi/casuali/casuale1.txt\
	-r ${path}/file/testi/casuali/casuale1.txt -d lettiT1r\
	-u ${path}/file/testi/casuali/casuale1.txt

${client} -f ${sock} -p -t 200 -l ${path}/file/testi/casuali/casuale2.txt\
	-c ${path}/file/testi/casuali/casuale2.txt\
	-a ${path}/file/testi/prova.txt,"Come stai?"\
	-r ${path}/file/testi/prova.txt -d lettiT1


kill -s SIGHUP $pid

wait $pid

exit 0

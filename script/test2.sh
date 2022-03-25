#!/bin/bash

server=./bin/server

echo "________________________________________________________________________"

#avvio server in background, salvando il pid 
${server} -f test/configt2fifo.txt &
pid=$!
echo "Server avviato PID:${pid} -- configurato con politica di rimpiazzamento FIFO"

sleep 1

client=./bin/client
sock=LSOfilestorage.sk
path="$( pwd -P )"

#avvio diversi client per testare la politica FIFO
echo "Avvio Client"

${client} -f ${sock} -p -t 200 -W file/testi/filegrandi/piccolo.txt -D espulsiT2fifo 
#size : 65534

${client} -f ${sock} -p -t 200 -w file/testi/filegrandi/grandix -D espulsiT2fifo 
#size : +792.291 = 857.825

${client} -f ${sock} -p -t 200 -W file/testi/racconto.txt,file/testi/filegrandi/grande.txt -D espulsiT2fifo 
#size : +61.049 = 918.874 +244.173 = 1.163.047 -> 2 espulsioni a partire dal primo file inserito in lista dalle precedenti operazioni

sleep 1

kill -s SIGHUP $pid

wait $pid

./script/statistiche.sh logging2fifo.log
	
echo "________________________________________________________________________"

#avvio server in background, salvando il pid 
${server} -f test/configt2lru.txt &
pid=$!
echo "Server avviato ${pid} -- configurato con politica di rimpiazzamento LRU"

sleep 1

#avvio diversi client per testare la politica LRU
echo "Avvio Client"

${client} -f ${sock} -p -t 200 -w file/testi/filegrandi/grandix -D espulsiT2lru
#size : 792.291
#questa scrittura inserisce in ordine i file: grandex.txt, grandex5.tx, grandex6.txt, grandex7.txt, grandex3.txt, grandex2.txt

${client} -f ${sock} -p -t 200 -W file/testi/racconto.txt,file/testi/filegrandi/piccolo.txt -D espulsiT2lru \
	-r ${path}/file/testi/filegrandi/grandix/grandex5.txt,${path}/file/testi/filegrandi/grandix/grandex.txt -d lettiT2lru
#size : +61.049 +65.534 = 918.874

${client} -f ${sock} -p -t 200 -W file/testi/filegrandi/grande.txt -D espulsiT2lru
#size : + 244.173 = 1.163.047 -> 1 espulsione, primo della lista che ha riferimento minore

sleep 1

kill -s SIGHUP $pid

wait $pid

./script/statistiche.sh logging2lru.log

echo "________________________________________________________________________"

#avvio server in background, salvando il pid 
${server} -f test/configt2lfu.txt &
pid=$!
echo "Server avviato ${pid} -- configurato con politica di rimpiazzamento LFU"

sleep 1

#avvio diversi client per testare la politica LFU
echo "Avvio Client"

${client} -f ${sock} -p -t 200 -w file/testi/filegrandi/grandix -D espulsiT2lfu
#size : 792.291
#questa scrittura inserisce in ordine i file: grandex.txt, grandex5.tx, grandex6.txt, grandex7.txt, grandex3.txt, grandex2.txt

${client} -f ${sock} -p -t 200 -W file/testi/racconto.txt,file/testi/filegrandi/piccolo.txt -D espulsiT2lfu \
	-r ${path}/file/testi/filegrandi/grandix/grandex6.txt,${path}/file/testi/filegrandi/grandix/grandex.txt -d lettiT2lfu
#size : +61.049 +65.534 = 918.874

${client} -f ${sock} -p -t 200 -W file/testi/filegrandi/grande.txt -D espulsiT2lfu
#size : + 244.173 = 1.163.047 -> 1 espulsione, primo della lista che ha riferimento minore

sleep 1

kill -s SIGHUP $pid

wait $pid

./script/statistiche.sh logging2lfu.log

exit 0

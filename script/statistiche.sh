#!/bin/bash

if [ $# -eq 0 ]; then 
  	echo "usage: $0 pathLogFile"
  	exit 1
fi

opentot=0
readtot=0
writetot=0
locktot=0
unlocktot=0
closetot=0

bytesread=0
byteswritten=0

nthread=0
maxconn=0
maxsizefs=0
maxnfilefs=0
replace=0

logfile=$1

if [ -e $logfile ]; then
	
	#filtro solo le righe che iniziano per '[' togliendo la parte dell'ora iniziale '--hh:mm:ss--'
	statCont=$(cat "${logfile}" | cut -c 13- | grep -e '\[')
	echo "$statCont" > log
	
	#CONTO IL N. DI OPERAZIONI
	opentot=$(grep -o "\[OPEN\]" "${logfile}" | wc -l)
	readtot=$(grep -o "\[READ\]" "${logfile}" | wc -l)
	writetot=$(grep -o "\[WRITE\]" "${logfile}" | wc -l)
	locktot=$(grep -o "\[LOCK\]" "${logfile}" | wc -l)
	unlocktot=$(grep -o "\[UNLOCK\]" "${logfile}" | wc -l)
	closetot=$(grep -o "\[CLOSE\]" "${logfile}" | wc -l)
	
	#CONTO N. BYTES LETTI
	for i in $(grep -e "\[READ\]" log | cut -c 8-); do
		bytesread=$bytesread+$i;
	done
	bytesread=$(bc <<< ${bytesread})
	#CONTO N. BYTES SCRITTI
	for i in $(grep -e "\[WRITE\]" log | cut -c 9-); do
		byteswritten=$byteswritten+$i;
	done
	byteswritten=$(bc <<< ${byteswritten})
	
	#SALVO INFO STAMPATE A TERMINE ESECUZIONE SERVER
	maxconn=$(grep -e "\[MAXCONNECTIONS\]" log | cut -c 18-)
	maxsizefs=$(grep -e "\[MAXSIZE\]" log | cut -c 11-)
	maxnfilefs=$(grep -e "\[MAXNFILE\]" log | cut -c 12-)
	replace=$(grep -e "\[REPLACE\]" log | cut -c 11-)
		
	#STAMPA SU STDOUT INFO DI STATISTICA RICHIESTE
	echo "STATISTICHE ESECUZIONE SERVER"	
	
	echo "N. operazioni di READ: ${readtot}"
	if [ ${readtot} != 0 ]; then
		avg=$(bc <<< "scale=2; ${bytesread} / ${readtot}")
		echo "size media delle letture in bytes: ${avg}"
	fi	
	
	echo "N. operazioni di WRITE: ${writetot}"
	if [ ${writetot} != 0 ]; then
		avg=$(bc <<< "scale=2; ${byteswritten} / ${writetot}")
		echo "size media delle scritture in bytes: ${avg}"
	fi	
	
	echo "N. operazioni di LOCK: ${locktot}"
	echo "N. operazioni di OPEN: ${opentot}"
	echo "N. operazioni di UNLOCK: ${unlocktot}"
	echo "N. operazioni di CLOSE: ${closetot}"	
	
	echo "Dimensione massima in Size raggiunta dal FileStorage: ${maxsizefs}"
	echo "Dimensione massima in Nfiles raggiunta dal FileStorage: ${maxnfilefs}"
	echo "Numero di applicazioni dell'algoritmo di rimpiazzamento della cache: ${replace}"	
	
	echo "Numero di richieste servite da ogni thread Worker:"	
	grep -oP "\[WORKERID\] [^,]*" "${logfile}" | sort | uniq -c
	
	echo "Numero massimo di connessioni contemporanee: ${maxconn}"	
	
	rm log	
	
   else 
	echo "Server non ancora eseguito"
fi

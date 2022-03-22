#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include <util.h>
#include <loggingserver.h>


/**
 *
 * @file loggingserver.c
 * @brief File di implementazione dell'interfaccia server per la funzionalità di logging
 *
 */
 
#define MAX_TIME_LEN 40

pthread_mutex_t logmtx = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief procedura che stampa sul file di logging, una stringa relativa all'operazione eseguita con il rispettivo timestamp
 * @param format: stringa del formato come *printf 
 * @param ...: variabili da sostituire nel format
 *
 */
void logEvent(const char *format, ...){
	time_t currentTime;
	struct tm* infoTime;
	char stringTime[MAX_TIME_LEN];
	char bufInfo[BUFSIZ];
	
	//definisco la stringa relativa all'operazione da registrare
	va_list ap;
	va_start(ap, format);
	//scrivo sul buffer i valori passati come parametro 
	vsnprintf(bufInfo, BUFSIZ, format, ap);
	va_end(ap);
	
	//timestamp attuale
	time(&currentTime);
	infoTime = localtime(&currentTime);
	
	LOCK(&logmtx);
	//sezione critica per il FILE logFile, funzione chiamata da diversi thread: mainThread e WorksersThread
	if(logFile){	
		//conversione timestamp attuale in formato leggibile
		strftime(stringTime, MAX_TIME_LEN, "%H:%M:%S", infoTime);
		//stampa sul file di log
		fprintf(logFile, "--%s-- %s\n", stringTime, bufInfo);
		fflush(logFile);
	}
	UNLOCK(&logmtx);
}

void logLock(const char* format, ...){
	char bufInfo[BUFSIZ];
	struct timespec tstart={0,0};
    	clock_gettime(CLOCK_MONOTONIC, &tstart);
    	
	//definisco la stringa relativa all'operazione da registrare
	va_list ap;
	va_start(ap, format);
	//scrivo sul buffer i valori passati come parametro 
	vsnprintf(bufInfo, BUFSIZ, format, ap);
	va_end(ap);
	
	LOCK(&logmtx);
	//sezione critica per il FILE logFile, funzione chiamata da diversi thread: mainThread e WorksersThread
	if(logFile){	
		
		fprintf(logFile, "[%f] %s\n", (double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec, bufInfo);
		fflush(logFile);
	}
	UNLOCK(&logmtx);
}


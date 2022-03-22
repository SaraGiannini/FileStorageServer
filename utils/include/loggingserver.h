#if !defined(LOGGING_SERVER_H_)
#define LOGGING_SERVER_H_


/**
 *
 * @file Header per la funzionalità di logging del Server
 *
 */

#include <stdio.h>
#include <stdarg.h>

FILE* logFile;


/**
 * @brief procedura che stampa sul file di logging, una stringa relativa all'operazione eseguita con il rispettivo timestamp
 * @param format: stringa del formato come *printf 
 * @param ...: variabili da sostituire nel format
 *
 */
void logEvent(const char *format, ...);

void logLock(const char* format, ...);

#endif /* LOGGING_SERVER_H_ */

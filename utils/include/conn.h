#if !defined(CONN_H)
#define CONN_H

/**
 *
 * @file Header per la connessione Client-Server
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#if !defined(UNIX_PATH_MAX)
#define UNIX_PATH_MAX 108
#endif
#if !defined(MAXBACKLOG)
#define MAXBACKLOG 10
#endif

//codici delle richieste di operazioni su file che il Client può fare al Server
#define OPENFILE 0
#define READFILE 1
#define READNFILE 2
#define WRITEFILE 3
#define APPENDFILE 4
#define LOCKFILE 5
#define UNLOCKFILE 6
#define CLOSEFILE 7
#define REMOVEFILE 8
#define RESTORE 9 //ripristina stato FS a prima dell openFile se writeFile non ha avuto successo lato server o lato client

//codici delle risposte del server a seguito della gestione delle richieste
#define OK 0
#define NOSUCHFILE -101
#define FILETOOBIG -102
#define INVALIDARGS -103
#define ALREADYEXIST -104
#define PERMDENIED -105
#define FILEREMOVED -106

#define HELP -200

//flag per openFile
#define O_CREATE 1
#define O_LOCK 2


/**
 *
 * @struct crequest_t
 * @brief  richiesta che il Client invia al Server per eseguire un'operazione
 *
 */
typedef struct clientrequest{
	int op;			//codice di richiesta 
	size_t lenpath;		//lunghezza path assoluto del file oggetto della richiesta
	size_t contentsize;	//dimensione del contenuto del file
	int flags;		//intero usato per flags O_CREATE O_LOCK settati per la richiesta e per numero di file richiesti per la lettura
} crequest_t;


/**
 *
 * @struct sreply_t
 * @brief  risposta che il Server invia al Client una volta eseguita l'operazione richiesta
 *		struttura usata sia per inviare i file letti sia per inviare i file espulsi a seguito di una capacity miss 
 *
 */
typedef struct serverreply{
	size_t lenpath;		//lunghezza path assoluto del file da inviare, letto o espulso
	size_t contentsize;	//dimensione del contenuto del file da inviare 
} sreturnfile_t;


/** 
	@brief: Legge n bytes nell'oggetto puntato da ptr dal socket con descrittore fd
	
	@param fd: descrittore del socket da cui leggere i bytes
	@param ptr: puntatore al buffer da usare per salvare i byte letti
	@param n: numero di bytes da leggere
	
	@return: numero fi bytes letti, -1 in caso di errore
*/
static inline ssize_t readn(int fd, void *ptr, size_t n){
	size_t nleft;
	ssize_t nread;
	nleft = n;
	while(nleft > 0){
		if((nread = read(fd, ptr, nleft)) < 0){
			if(nleft == n) return -1; //errore
			else break; //errore, ritorna #byte letti fin'ora
		}
		else if(nread == 0) break; //EOF
		nleft -= nread;
		ptr = (char*)ptr + nread;
	}
	return (n - nleft);
}

/** 
	@brief: Scrive n bytes dell'oggetto puntato da ptr sul socket con descrittore fd
	
	@param fd: descrittore del socket a cui inviare i bytes
	@param ptr: puntatore al buffer da inviare
	@param n: numero di bytes da scrivere
	
	@return: numero di bytes scritti, -1 in caso di errore
*/
static inline ssize_t writen(int fd, void *ptr, size_t n){
	size_t nleft;
	ssize_t nwritten;
	nleft = n;
	while(nleft > 0){
		if((nwritten = write(fd, ptr, nleft)) < 0){
			if(nleft == n) return -1; //errore
			else break; //errore, ritorna #byte scritti fin'ora
		}
		else if(nwritten == 0) break;
		nleft -= nwritten;
		ptr = (char*)ptr + nwritten;
	}
	return (n - nleft);
}

#endif /* CONN_H */

#define _POSIX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <util.h>
#include <conn.h>
#include <signalhserver.h>



/* funzione eseguita dal thread che gestisce i segnali */
void *sighandlerF(void* args){
	sigset_t* mask = ((sigHArgs_t*)args)->mask; //maschera dei segnali bloccati, preparata nel main thread
	int* terminationServer = ((sigHArgs_t*)args)->terminationServer;
	int* stopRequests = ((sigHArgs_t*)args)->stopRequests;
	pthread_mutex_t* sigmtx = ((sigHArgs_t*)args)->sigmtx;
	int fdmain = ((sigHArgs_t*)args)->fdmain;
	
	int err, sig;
	
	CHECK_NEQ_RETURN(sigwait(mask, &sig), 0, "sigwait", NULL);
	switch(sig){
	case SIGINT:
	case SIGQUIT:
		fprintf(stdout, "\n\t***** Ricevuto segnale %s *****\n", (sig == SIGINT) ? "SIGINT": "SIGQUIT");
		LOCK(sigmtx);
		*terminationServer = 1;
		UNLOCK(sigmtx);
		SC_EXIT(err, close(fdmain), "close"); //notifico il main thread (in lettura) della ricezione del segnale, per sbloccare la select
		return NULL;
	case SIGHUP:
		fprintf(stdout, "\n\t***** Ricevuto segnale SIGHUP *****\n");
		LOCK(sigmtx);
		*stopRequests = 1;
		UNLOCK(sigmtx);
		SC_EXIT(err, close(fdmain), "close"); //notifico il main thread (in lettura) della ricezione del segnale, per sbloccare la select
		return NULL;
	default: ;
	}
	return NULL;
}
//procedura per settare il flag di terminazione del Server in mutua esclusione 
void terminazione(pthread_mutex_t mtx, int *termina){
	LOCK(&mtx);
	*termina = 1;
	UNLOCK(&mtx);
}
//funzione per controllare se è stato catturato un segnale e gestito, in mutua esclusione
int iscaught(pthread_mutex_t mtx, int flag){
	int ret;
	LOCK(&mtx);
	ret = flag;
	UNLOCK(&mtx);
	return ret;
}


